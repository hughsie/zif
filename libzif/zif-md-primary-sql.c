/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:zif-md-primary-sql
 * @short_description: Primary metadata
 *
 * Provide access to the primary repo metadata.
 * This object is a subclass of #ZifMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <gio/gio.h>

#include "zif-config.h"
#include "zif-depend-private.h"
#include "zif-md.h"
#include "zif-md-primary-sql.h"
#include "zif-package-array-private.h"
#include "zif-package-remote.h"
#include "zif-state-private.h"
#include "zif-utils-private.h"

#define ZIF_MD_PRIMARY_SQL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_PRIMARY_SQL, ZifMdPrimarySqlPrivate))

/* sqlite has a maximum of about 1000, but optimum seems about 300 */
#define ZIF_MD_PRIMARY_SQL_MAX_EXPRESSION_DEPTH 300

/**
 * ZifMdPrimarySqlPrivate:
 *
 * Private #ZifMdPrimarySql data
 **/
struct _ZifMdPrimarySqlPrivate
{
	gboolean		 loaded;
	sqlite3			*db;
	ZifConfig		*config;
	GHashTable		*conflicts_name;
	GHashTable		*obsoletes_name;
};

typedef struct {
	const gchar		*id;
	GPtrArray		*packages;
	ZifMdPrimarySql		*md;
	ZifPackageCompareMode	 compare_mode;
} ZifMdPrimarySqlData;

G_DEFINE_TYPE (ZifMdPrimarySql, zif_md_primary_sql, ZIF_TYPE_MD)

/**
 * zif_md_primary_sql_unload:
 **/
static gboolean
zif_md_primary_sql_unload (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_primary_sql_sqlite_name_depends_cb:
 **/
static gint
zif_md_primary_sql_sqlite_name_depends_cb (void *data,
					   gint argc,
					   gchar **argv,
					   gchar **col_name)
{
	GHashTable *hash = (GHashTable *) data;
	g_hash_table_insert (hash,
			     g_strdup (argv[0]),
			     GINT_TO_POINTER (1));
	return 0;
}

/**
 * zif_md_primary_sql_load:
 **/
static gboolean
zif_md_primary_sql_load (ZifMd *md, ZifState *state, GError **error)
{
	const gchar *filename;
	const gchar *statement;
	gchar *error_msg = NULL;
	gint rc;
	ZifMdPrimarySql *primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* already loaded */
	if (primary_sql->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for primary_sql");
		goto out;
	}

	/* open database */
	zif_state_set_allow_cancel (state, FALSE);
	g_debug ("filename = %s", filename);
	rc = sqlite3_open (filename, &primary_sql->priv->db);
	if (rc != 0) {
		g_warning ("Can't open database: %s\n", sqlite3_errmsg (primary_sql->priv->db));
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "can't open database: %s", sqlite3_errmsg (primary_sql->priv->db));
		goto out;
	}

	/* we don't need to keep syncing */
	sqlite3_exec (primary_sql->priv->db, "PRAGMA synchronous=OFF;", NULL, NULL, NULL);

	/* populate the obsoletes name cache */
	statement = "SELECT name FROM obsoletes;";
	rc = sqlite3_exec (primary_sql->priv->db, statement,
			   zif_md_primary_sql_sqlite_name_depends_cb,
			   primary_sql->priv->obsoletes_name,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* populate the conflicts name cache */
	statement = "SELECT name FROM conflicts;";
	rc = sqlite3_exec (primary_sql->priv->db, statement,
			   zif_md_primary_sql_sqlite_name_depends_cb,
			   primary_sql->priv->conflicts_name,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	primary_sql->priv->loaded = TRUE;
out:
	return primary_sql->priv->loaded;
}

/**
 * zif_md_primary_sql_sqlite_create_package_cb:
 **/
static gint
zif_md_primary_sql_sqlite_create_package_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	ZifMdPrimarySqlData *fldata = (ZifMdPrimarySqlData *) data;
	ZifPackage *package;
	ZifStoreRemote *store_remote;
	gboolean ret;

	package = zif_package_remote_new ();
	store_remote = ZIF_STORE_REMOTE (zif_md_get_store (ZIF_MD (fldata->md)));
	if (store_remote != NULL) {
		/* this is not set in a test harness */
		zif_package_remote_set_store_remote (ZIF_PACKAGE_REMOTE (package),
						     store_remote);
	} else {
		g_debug ("no remote store for %s, which is okay as we're in make check",
			 argv[1]);
	}
	zif_package_set_compare_mode (package, fldata->compare_mode);

	/* add */
	ret = zif_package_remote_set_from_repo (ZIF_PACKAGE_REMOTE (package),
						argc,
						col_name,
						argv,
						fldata->id,
						NULL);
	if (ret) {
		g_ptr_array_add (fldata->packages, package);
	} else {
		g_warning ("failed to add: %s", argv[1]);
		g_object_unref (package);
	}

	return 0;
}

#define ZIF_MD_PRIMARY_SQL_HEADER "SELECT p.pkgId, p.name, p.arch, p.version, " \
				  "p.epoch, p.release, p.summary, p.description, p.url, " \
				  "p.rpm_license, p.rpm_group, p.size_package, " \
				  "p.location_href, p.rpm_sourcerpm, "\
				  "p.time_file FROM packages p"

/**
 * zif_md_primary_sql_search:
 **/
static GPtrArray *
zif_md_primary_sql_search (ZifMdPrimarySql *md, const gchar *statement,
			   ZifState *state, GError **error)
{
	gchar *error_msg = NULL;
	gint rc;
	gboolean ret;
	GError *error_local = NULL;
	ZifMdPrimarySqlData *data = NULL;
	GPtrArray *array = NULL;

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), state, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_MD_ERROR,
				     ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_primary_sql file: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* create data struct we can pass to the callback */
	zif_state_set_allow_cancel (state, FALSE);
	data = g_new0 (ZifMdPrimarySqlData, 1);
	data->md = md;
	data->id = zif_md_get_id (ZIF_MD (md));

	/* get the compare mode */
	data->compare_mode = zif_config_get_enum (md->priv->config,
						  "pkg_compare_mode",
						  zif_package_compare_mode_from_string,
						  error);
	if (data->compare_mode == G_MAXUINT)
		goto out;

	data->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	if (g_getenv ("ZIF_SQL_DEBUG") != NULL) {
		g_debug ("On %s\n%s",
			 zif_md_get_filename_uncompressed (ZIF_MD (md)),
			 statement);
	}
	rc = sqlite3_exec (md->priv->db, statement,
			   zif_md_primary_sql_sqlite_create_package_cb,
			   data, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		g_ptr_array_unref (data->packages);
		goto out;
	}
	/* list of packages */
	array = data->packages;
out:
	g_free (data);
	return array;
}

/**
 * zif_md_primary_sql_get_statement_for_pred:
 **/
static gchar *
zif_md_primary_sql_get_statement_for_pred (const gchar *pred,
					   gchar **search,
					   gboolean use_glob)
{
	const guint max_items = 20;
	gchar **search_noarch = NULL;
	gchar *tmp;
	GString *pred_glob;
	GString *statement;
	GString *temp;
	guint i;

	/* use stripped arch? */
	if (g_strstr_len (pred, -1, "$NOARCH") != NULL) {
		search_noarch = g_strdupv (search);
		for (i = 0; search[i] != NULL; i++) {
			tmp = strrchr (search_noarch[i], '.');
			if (tmp != NULL)
				*tmp = '\0';
		}
	}

	/* glob? */
	pred_glob = g_string_new (pred);
	if (use_glob) {
		zif_string_replace (pred_glob,
				    "$MATCH",
				    "GLOB");
	} else {
		zif_string_replace (pred_glob,
				    "$MATCH",
				    "=");
	}

	/* search with predicate */
	statement = g_string_new ("BEGIN;\n");
	for (i = 0; search[i] != NULL; i++) {
		if (i % max_items == 0)
			g_string_append (statement, ZIF_MD_PRIMARY_SQL_HEADER " WHERE ");
		temp = g_string_new (pred_glob->str);
		zif_string_replace (temp,
				    "$SEARCH",
				    search[i]);
		if (search_noarch != NULL) {
			zif_string_replace (temp,
					    "$NOARCH",
					    search_noarch[i]);
		}
		g_string_append (statement, temp->str);
		if (i % max_items == max_items - 1)
			g_string_append (statement, ";\n");
		else
			g_string_append (statement, " OR ");
		g_string_free (temp, TRUE);
	}

	/* remove trailing OR entry */
	if (g_str_has_suffix (statement->str, " OR ")) {
		g_string_set_size (statement, statement->len - 4);
		g_string_append (statement, ";\n");
	}
	g_string_append (statement, "END;");
	g_string_free (pred_glob, TRUE);
	g_strfreev (search_noarch);
	return g_string_free (statement, FALSE);
}

/**
 * zif_md_primary_sql_resolve:
 **/
static GPtrArray *
zif_md_primary_sql_resolve (ZifMd *md,
			    gchar **search,
			    ZifStoreResolveFlags flags,
			    ZifState *state,
			    GError **error)
{
	gboolean use_glob = FALSE;
	gboolean ret;
	gchar *statement;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	GPtrArray *tmp;
	guint cnt = 0;
	guint i;
	ZifState *state_local;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (flags != 0, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find out how many steps we need to do */
	cnt += ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME) > 0);
	cnt += ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH) > 0);
	cnt += ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION) > 0);
	cnt += ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION_ARCH) > 0);
	zif_state_set_number_steps (state, cnt);

	/* we don't support regular expressions */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_REGEX) > 0) {
		g_set_error_literal (error,
				     ZIF_MD_ERROR,
				     ZIF_MD_ERROR_NO_SUPPORT,
				     "Regular expressions are not supported");
		goto out;
	}

	/* support globbing? */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_GLOB) > 0)
		use_glob = TRUE;

	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* name */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME) > 0) {
		statement = zif_md_primary_sql_get_statement_for_pred ("p.name $MATCH '$SEARCH'",
								       search,
								       use_glob);
		state_local = zif_state_get_child (state);
		tmp = zif_md_primary_sql_search (md_primary_sql,
						 statement,
						 state_local,
						 error);
		g_free (statement);
		if (tmp == NULL)
			goto out;
		for (i = 0; i < tmp->len; i++)
			g_ptr_array_add (array_tmp, g_object_ref (g_ptr_array_index (tmp, i)));
		g_ptr_array_unref (tmp);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* name.arch */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH) > 0) {
		statement = zif_md_primary_sql_get_statement_for_pred ("(p.name||'.'||"
								       "p.arch $MATCH '$SEARCH')"
								       " OR "
								       "(p.name $MATCH '$NOARCH' AND "
								       "p.arch $MATCH 'noarch')",
								       search,
								       use_glob);
		state_local = zif_state_get_child (state);
		tmp = zif_md_primary_sql_search (md_primary_sql,
						 statement,
						 state_local,
						 error);
		g_free (statement);
		if (tmp == NULL)
			goto out;
		for (i = 0; i < tmp->len; i++)
			g_ptr_array_add (array_tmp, g_object_ref (g_ptr_array_index (tmp, i)));
		g_ptr_array_unref (tmp);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* name-version */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION) > 0) {
		statement = zif_md_primary_sql_get_statement_for_pred ("p.name||'-'||"
								       "p.version||'-'||"
								       "p.release $MATCH '$SEARCH'",
								       search,
								       use_glob);
		state_local = zif_state_get_child (state);
		tmp = zif_md_primary_sql_search (md_primary_sql,
						 statement,
						 state_local,
						 error);
		g_free (statement);
		if (tmp == NULL)
			goto out;
		for (i = 0; i < tmp->len; i++)
			g_ptr_array_add (array_tmp, g_object_ref (g_ptr_array_index (tmp, i)));
		g_ptr_array_unref (tmp);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* name-version.arch */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION_ARCH) > 0) {
		statement = zif_md_primary_sql_get_statement_for_pred ("p.name||'-'||"
								       "p.version||'-'||"
								       "p.release||'.'||"
								       "p.arch $MATCH '$SEARCH'",
								       search,
								       use_glob);
		state_local = zif_state_get_child (state);
		tmp = zif_md_primary_sql_search (md_primary_sql,
						 statement,
						 state_local,
						 error);
		g_free (statement);
		if (tmp == NULL)
			goto out;
		for (i = 0; i < tmp->len; i++)
			g_ptr_array_add (array_tmp, g_object_ref (g_ptr_array_index (tmp, i)));
		g_ptr_array_unref (tmp);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * zif_md_primary_sql_search_name:
 **/
static GPtrArray *
zif_md_primary_sql_search_name (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* fuzzy name match */
	statement = zif_md_primary_sql_get_statement_for_pred ("p.name LIKE '%%$SEARCH%%'",
							       search,
							       FALSE);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_search_details:
 **/
static GPtrArray *
zif_md_primary_sql_search_details (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* fuzzy details match */
	statement = zif_md_primary_sql_get_statement_for_pred ("p.name LIKE '%%$SEARCH%%' OR "
							       "p.summary LIKE '%%$SEARCH%%' OR "
							       "p.description LIKE '%%$SEARCH%%'",
							       search,
							       FALSE);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_search_group:
 **/
static GPtrArray *
zif_md_primary_sql_search_group (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* simple group match */
	statement = zif_md_primary_sql_get_statement_for_pred ("p.rpm_group = '$SEARCH'",
							       search,
							       FALSE);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_search_pkgid:
 **/
static GPtrArray *
zif_md_primary_sql_search_pkgid (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* simple pkgid match */
	statement = zif_md_primary_sql_get_statement_for_pred ("p.pkgid = '$SEARCH'",
							       search,
							       FALSE);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_sqlite_depend_cb:
 **/
static gint
zif_md_primary_sql_sqlite_depend_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	ZifDepend *depend;
	GPtrArray *array = (GPtrArray *) data;
	depend = zif_depend_new_from_data_full ((const gchar **)col_name,
						(const gchar **)argv,
						argc);
	if (depend != NULL)
		g_ptr_array_add (array, depend);
	return 0;
}

/**
 * zif_md_primary_sql_what_depends:
 **/
static GPtrArray *
zif_md_primary_sql_what_depends (ZifMd *md,
				 const gchar *table_name,
				 GPtrArray *depends,
				 ZifState *state,
				 GError **error)
{
	gboolean ret;
	gchar *error_msg = NULL;
	GError *error_local = NULL;
	GHashTable *hash_tmp = NULL;
	gint rc;
	GPtrArray *array = NULL;
	GPtrArray *depends2 = NULL;
	GString *statement = NULL;
	guint i, j;
	ZifDepend *depend_tmp;
	ZifMdPrimarySqlData *data = NULL;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);
	ZifPackageEnsureType ensure_type = ZIF_PACKAGE_ENSURE_TYPE_LAST;
	ZifState *state_local;

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup steps */
	if (md_primary_sql->priv->loaded) {
		ret = zif_state_set_steps (state,
					   error,
					   90, /* sql query */
					   10, /* filter */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   10, /* search */
					   10, /* filter */
					   -1);
	}
	if (!ret)
		goto out;

	/* if not already loaded, load */
	if (!md_primary_sql->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_md_load (md, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_primary_sql file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* a struct to return results */
	data = g_new0 (ZifMdPrimarySqlData, 1);
	data->md = md_primary_sql;
	data->id = zif_md_get_id (ZIF_MD (md));
	data->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* convert to enum type */
	if (g_strcmp0 (table_name, "requires") == 0) {
		ensure_type = ZIF_PACKAGE_ENSURE_TYPE_REQUIRES;
	} else if (g_strcmp0 (table_name, "provides") == 0) {
		ensure_type = ZIF_PACKAGE_ENSURE_TYPE_PROVIDES;
	} else if (g_strcmp0 (table_name, "conflicts") == 0) {
		ensure_type = ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS;
		hash_tmp = md_primary_sql->priv->conflicts_name;
	} else if (g_strcmp0 (table_name, "obsoletes") == 0) {
		ensure_type = ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES;
		hash_tmp = md_primary_sql->priv->obsoletes_name;
	} else {
		g_assert_not_reached ();
	}

	/* can we limit the size of the SQL statement by removing
	 * queries with names that we know are not in the table */
	depends2 = g_ptr_array_new ();
	for (i = 0; i < depends->len; i++) {
		depend_tmp = g_ptr_array_index (depends, i);
		if (hash_tmp != NULL &&
		    g_hash_table_lookup (hash_tmp,
					 zif_depend_get_name (depend_tmp)) == NULL) {
			continue;
		}
		g_ptr_array_add (depends2, depend_tmp);
	}

	/* create one super huge statement with 'ORs' rather than doing
	 * thousands of indervidual queries */
	statement = g_string_new ("");
	g_string_append (statement, "BEGIN;\n");

	for (j = 0; j < depends2->len; j += ZIF_MD_PRIMARY_SQL_MAX_EXPRESSION_DEPTH) {
		g_string_append_printf (statement, ZIF_MD_PRIMARY_SQL_HEADER ", %s depend WHERE "
					"p.pkgKey = depend.pkgKey AND (",
					table_name);

		for (i = j; i < depends2->len && (i-j) < ZIF_MD_PRIMARY_SQL_MAX_EXPRESSION_DEPTH; i++) {
			depend_tmp = g_ptr_array_index (depends2, i);
			g_string_append_printf (statement, "depend.name = '%s' OR ",
						zif_depend_get_name (depend_tmp));
		}
		/* remove trailing OR */
		g_string_set_size (statement, statement->len - 4);
		g_string_append (statement, ");\n");
	}

	/* a package always provides itself, even without an explicit provide */
	if (ensure_type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {
		for (j = 0; j < depends2->len; j+= ZIF_MD_PRIMARY_SQL_MAX_EXPRESSION_DEPTH) {
			g_string_append (statement, ZIF_MD_PRIMARY_SQL_HEADER " WHERE ");

			for (i=j; i < depends2->len && (i-j)<ZIF_MD_PRIMARY_SQL_MAX_EXPRESSION_DEPTH; i++) {
				depend_tmp = g_ptr_array_index (depends2, i);
				g_string_append_printf (statement, "p.name = '%s' OR ",
							zif_depend_get_name (depend_tmp));
			}
			/* remove trailing OR */
			g_string_set_size (statement, statement->len - 4);
			g_string_append (statement, ";\n");
		}
	}

	g_string_append (statement, "END;\n");

	/* execute the query */
	if (g_getenv ("ZIF_SQL_DEBUG") != NULL) {
		g_debug ("On %s\n%s",
			 zif_md_get_filename_uncompressed (md),
			 statement->str);
	}
	rc = sqlite3_exec (md_primary_sql->priv->db,
			   statement->str,
			   zif_md_primary_sql_sqlite_create_package_cb,
			   data,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	sqlite3_exec (md_primary_sql->priv->db, "END;", NULL, NULL, NULL);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* filter results */
	state_local = zif_state_get_child (state);
	if (ensure_type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {
		ret = zif_package_array_filter_provide (data->packages,
							depends2,
							state_local,
							error);
	} else if (ensure_type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {
		ret = zif_package_array_filter_require (data->packages,
							depends2,
							state_local,
							error);
	} else if (ensure_type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {
		ret = zif_package_array_filter_obsolete (data->packages,
							 depends2,
							 state_local,
							 error);
	} else if (ensure_type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {
		ret = zif_package_array_filter_conflict (data->packages,
							 depends2,
							 state_local,
							 error);
	} else {
		g_assert_not_reached ();
	}
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	array = g_ptr_array_ref (data->packages);
out:
	if (data != NULL) {
		g_ptr_array_unref (data->packages);
		g_free (data);
	}
	if (depends2 != NULL)
		g_ptr_array_unref (depends2);
	if (statement != NULL)
		g_string_free (statement, TRUE);
	return array;
}

/**
 * zif_md_primary_sql_what_provides:
 **/
static GPtrArray *
zif_md_primary_sql_what_provides (ZifMd *md, GPtrArray *depends,
				  ZifState *state, GError **error)
{
	return zif_md_primary_sql_what_depends (md, "provides", depends, state, error);
}

/**
 * zif_md_primary_sql_what_requires:
 **/
static GPtrArray *
zif_md_primary_sql_what_requires (ZifMd *md, GPtrArray *depends,
				  ZifState *state, GError **error)
{
	return zif_md_primary_sql_what_depends (md, "requires", depends, state, error);
}

/**
 * zif_md_primary_sql_what_obsoletes:
 **/
static GPtrArray *
zif_md_primary_sql_what_obsoletes (ZifMd *md, GPtrArray *depends,
				   ZifState *state, GError **error)
{
	return zif_md_primary_sql_what_depends (md, "obsoletes", depends, state, error);
}

/**
 * zif_md_primary_sql_what_conflicts:
 **/
static GPtrArray *
zif_md_primary_sql_what_conflicts (ZifMd *md, GPtrArray *depends,
				   ZifState *state, GError **error)
{
	return zif_md_primary_sql_what_depends (md, "conflicts", depends, state, error);
}

/**
 * zif_md_primary_sql_get_depends:
 **/
static GPtrArray *
zif_md_primary_sql_get_depends (ZifMd *md,
				const gchar *type,
				ZifPackage *package,
				ZifState *state,
				GError **error)
{
	const gchar *epoch = NULL;
	const gchar *release = NULL;
	const gchar *version = NULL;
	gchar *error_msg = NULL;
	gchar *evr;
	gchar *statement = NULL;
	gint rc;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	evr = g_strdup (zif_package_get_version (package));
	zif_package_convert_evr (evr, &epoch, &version, &release);

	/* get depend array for the package */
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	statement = g_strdup_printf ("SELECT depend.name, depend.flags, depend.epoch, "
				     "depend.version, depend.release FROM %s depend, packages WHERE "
				     "packages.pkgKey = depend.pkgKey AND "
				     "packages.name = '%s' AND "
				     "packages.epoch = '%s' AND "
				     "packages.version = '%s' AND "
				     "packages.release = '%s' AND "
				     "packages.arch = '%s';",
				     type,
				     zif_package_get_name (package),
				     epoch != NULL ? epoch : "0",
				     version,
				     release,
				     zif_package_get_arch (package));
	if (g_getenv ("ZIF_SQL_DEBUG") != NULL) {
		g_debug ("On %s\n%s",
			 zif_md_get_filename_uncompressed (md),
			 statement);
	}
	rc = sqlite3_exec (md_primary_sql->priv->db,
			   statement,
			   zif_md_primary_sql_sqlite_depend_cb,
			   array_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	g_free (statement);
	g_free (evr);
	return array;
}

/**
 * zif_md_primary_sql_get_provides:
 **/
static GPtrArray *
zif_md_primary_sql_get_provides (ZifMd *md, ZifPackage *package,
				 ZifState *state, GError **error)
{
	return zif_md_primary_sql_get_depends (md, "provides", package, state, error);
}

/**
 * zif_md_primary_sql_get_requires:
 **/
static GPtrArray *
zif_md_primary_sql_get_requires (ZifMd *md, ZifPackage *package,
				 ZifState *state, GError **error)
{
	return zif_md_primary_sql_get_depends (md, "requires", package, state, error);
}

/**
 * zif_md_primary_sql_get_obsoletes:
 **/
static GPtrArray *
zif_md_primary_sql_get_obsoletes (ZifMd *md, ZifPackage *package,
				  ZifState *state, GError **error)
{
	return zif_md_primary_sql_get_depends (md, "obsoletes", package, state, error);
}

/**
 * zif_md_primary_sql_get_conflicts:
 **/
static GPtrArray *
zif_md_primary_sql_get_conflicts (ZifMd *md, ZifPackage *package,
				  ZifState *state, GError **error)
{
	return zif_md_primary_sql_get_depends (md, "conflicts", package, state, error);
}

/**
 * zif_md_primary_sql_find_package:
 **/
static GPtrArray *
zif_md_primary_sql_find_package (ZifMd *md, const gchar *package_id, ZifState *state, GError **error)
{
	gboolean ret;
	gchar *arch = NULL;
	gchar *name = NULL;
	gchar *release = NULL;
	gchar *statement = NULL;
	gchar *version = NULL;
	GPtrArray *array = NULL;
	guint epoch;

	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* split up */
	ret = zif_package_id_to_nevra (package_id,
				       &name, &epoch, &version,
				       &release, &arch);
	if (!ret) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_FAILED,
			     "invalid id: %s",
			     package_id);
		goto out;
	}

	/* search with predicate */
	statement = g_strdup_printf (ZIF_MD_PRIMARY_SQL_HEADER " WHERE p.name = '%s'"
				     " AND p.epoch = '%i'"
				     " AND p.version = '%s'"
				     " AND p.release = '%s'"
				     " AND p.arch = '%s'",
				     name, epoch, version, release, arch);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
out:
	g_free (statement);
	g_free (name);
	g_free (version);
	g_free (release);
	g_free (arch);
	return array;
}

/**
 * zif_md_primary_sql_get_packages:
 **/
static GPtrArray *
zif_md_primary_sql_get_packages (ZifMd *md, ZifState *state, GError **error)
{
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	array = zif_md_primary_sql_search (md_primary_sql, ZIF_MD_PRIMARY_SQL_HEADER, state, error);
	return array;
}

/**
 * zif_md_primary_sql_finalize:
 **/
static void
zif_md_primary_sql_finalize (GObject *object)
{
	ZifMdPrimarySql *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_PRIMARY_SQL (object));
	md = ZIF_MD_PRIMARY_SQL (object);

	sqlite3_close (md->priv->db);
	g_object_unref (md->priv->config);
	g_hash_table_unref (md->priv->conflicts_name);
	g_hash_table_unref (md->priv->obsoletes_name);

	G_OBJECT_CLASS (zif_md_primary_sql_parent_class)->finalize (object);
}

/**
 * zif_md_primary_sql_class_init:
 **/
static void
zif_md_primary_sql_class_init (ZifMdPrimarySqlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_primary_sql_finalize;

	/* map */
	md_class->load = zif_md_primary_sql_load;
	md_class->unload = zif_md_primary_sql_unload;
	md_class->search_name = zif_md_primary_sql_search_name;
	md_class->search_details = zif_md_primary_sql_search_details;
	md_class->search_group = zif_md_primary_sql_search_group;
	md_class->search_pkgid = zif_md_primary_sql_search_pkgid;
	md_class->what_provides = zif_md_primary_sql_what_provides;
	md_class->what_requires = zif_md_primary_sql_what_requires;
	md_class->what_obsoletes = zif_md_primary_sql_what_obsoletes;
	md_class->what_conflicts = zif_md_primary_sql_what_conflicts;
	md_class->get_provides = zif_md_primary_sql_get_provides;
	md_class->get_requires = zif_md_primary_sql_get_requires;
	md_class->get_obsoletes = zif_md_primary_sql_get_obsoletes;
	md_class->get_conflicts = zif_md_primary_sql_get_conflicts;
	md_class->resolve = zif_md_primary_sql_resolve;
	md_class->get_packages = zif_md_primary_sql_get_packages;
	md_class->find_package = zif_md_primary_sql_find_package;
	g_type_class_add_private (klass, sizeof (ZifMdPrimarySqlPrivate));
}

/**
 * zif_md_primary_sql_init:
 **/
static void
zif_md_primary_sql_init (ZifMdPrimarySql *md)
{
	md->priv = ZIF_MD_PRIMARY_SQL_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->db = NULL;
	md->priv->config = zif_config_new ();
	md->priv->conflicts_name =
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       NULL);
	md->priv->obsoletes_name =
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       NULL);
}

/**
 * zif_md_primary_sql_new:
 *
 * Return value: A new #ZifMdPrimarySql instance.
 *
 * Since: 0.1.0
 **/
ZifMd *
zif_md_primary_sql_new (void)
{
	ZifMdPrimarySql *md;
	md = g_object_new (ZIF_TYPE_MD_PRIMARY_SQL,
			   "kind", ZIF_MD_KIND_PRIMARY_SQL,
			   NULL);
	return ZIF_MD (md);
}

