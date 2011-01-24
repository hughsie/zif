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

#include "zif-md.h"
#include "zif-md-primary-sql.h"
#include "zif-package-array.h"
#include "zif-package-remote.h"
#include "zif-utils.h"

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
};

typedef struct {
	const gchar		*id;
	GPtrArray		*packages;
	ZifMdPrimarySql		*md;
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
 * zif_md_primary_sql_load:
 **/
static gboolean
zif_md_primary_sql_load (ZifMd *md, ZifState *state, GError **error)
{
	const gchar *filename;
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
	store_remote = zif_md_get_store_remote (ZIF_MD (fldata->md));
	if (store_remote != NULL) {
		/* this is not set in a test harness */
		zif_package_remote_set_store_remote (ZIF_PACKAGE_REMOTE (package), store_remote);
	} else {
		g_debug ("no remote store for %s, which is okay as we're in make check", argv[1]);
	}

	/* add */
	ret = zif_package_remote_set_from_repo (ZIF_PACKAGE_REMOTE (package), argc, col_name, argv, fldata->id, NULL);
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
				  "p.rpm_license, p.rpm_group, p.size_package, p.location_href, "\
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
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_primary_sql file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* create data struct we can pass to the callback */
	zif_state_set_allow_cancel (state, FALSE);
	data = g_new0 (ZifMdPrimarySqlData, 1);
	data->md = md;
	data->id = zif_md_get_id (ZIF_MD (md));
	data->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	rc = sqlite3_exec (md->priv->db, statement, zif_md_primary_sql_sqlite_create_package_cb, data, &error_msg);
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
 * zif_md_primary_sql_strreplace:
 **/
static gchar *
zif_md_primary_sql_strreplace (const gchar *text, const gchar *find, const gchar *replace)
{
	gchar **array;
	gchar *retval;

	/* split apart and rejoin with new delimiter */
	array = g_strsplit (text, find, 0);
	retval = g_strjoinv (replace, array);
	g_strfreev (array);
	return retval;
}

/**
 * zif_md_primary_sql_get_statement_for_pred:
 **/
static gchar *
zif_md_primary_sql_get_statement_for_pred (const gchar *pred, gchar **search)
{
	guint i;
	const guint max_items = 20;
	GString *statement;
	gchar *temp;

	/* search with predicate */
	statement = g_string_new ("BEGIN;\n");
	for (i=0; search[i] != NULL; i++) {
		if (i % max_items == 0)
			g_string_append (statement, ZIF_MD_PRIMARY_SQL_HEADER " WHERE ");
		temp = zif_md_primary_sql_strreplace (pred, "###", search[i]);
		g_string_append (statement, temp);
		if (i % max_items == max_items - 1)
			g_string_append (statement, ";\n");
		else
			g_string_append (statement, " OR ");
		g_free (temp);
	}

	/* remove trailing OR entry */
	if (g_str_has_suffix (statement->str, " OR ")) {
		g_string_set_size (statement, statement->len - 4);
		g_string_append (statement, ";\n");
	}
	g_string_append (statement, "END;");
	return g_string_free (statement, FALSE);
}

/**
 * zif_md_primary_sql_resolve:
 **/
static GPtrArray *
zif_md_primary_sql_resolve (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* simple name match */
	statement = zif_md_primary_sql_get_statement_for_pred ("p.name = '###'", search);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
	g_free (statement);
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
	statement = zif_md_primary_sql_get_statement_for_pred ("p.name LIKE '%%###%%'", search);
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
	statement = zif_md_primary_sql_get_statement_for_pred ("p.name LIKE '%%###%%' OR "
							       "p.summary LIKE '%%###%%' OR "
							       "p.description LIKE '%%###%%'", search);
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
	statement = zif_md_primary_sql_get_statement_for_pred ("p.rpm_group = '###'", search);
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
	statement = zif_md_primary_sql_get_statement_for_pred ("p.pkgid = '###'", search);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_flags_to_flag:
 **/
static ZifDependFlag
zif_md_primary_sql_flags_to_flag (const gchar *flags)
{
	if (flags == NULL)
		return ZIF_DEPEND_FLAG_ANY;
	if (g_strcmp0 (flags, "EQ") == 0)
		return ZIF_DEPEND_FLAG_EQUAL;
	if (g_strcmp0 (flags, "LT") == 0)
		return ZIF_DEPEND_FLAG_LESS;
	if (g_strcmp0 (flags, "GT") == 0)
		return ZIF_DEPEND_FLAG_GREATER;
	if (g_strcmp0 (flags, "LE") == 0)
		return ZIF_DEPEND_FLAG_LESS | ZIF_DEPEND_FLAG_EQUAL;
	if (g_strcmp0 (flags, "GE") == 0)
		return ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL;
	g_warning ("unknown flag string %s", flags);
	return ZIF_DEPEND_FLAG_UNKNOWN;
}

/**
 * zif_md_primary_sql_sqlite_depend_cb:
 **/
static gint
zif_md_primary_sql_sqlite_depend_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint i;
	GString *version;
	ZifDepend *depend;
	GPtrArray *array = (GPtrArray *) data;

	/* get the depend */
	version = g_string_new ("");
	depend = zif_depend_new ();
	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "name") == 0) {
			zif_depend_set_name (depend, argv[i]);
		} else if (g_strcmp0 (col_name[i], "epoch") == 0) {
			/* only add epoch if not zero */
			if (argv[i] != NULL && g_strcmp0 (argv[i], "0") != 0)
				g_string_append (version, argv[i]);
		} else if (g_strcmp0 (col_name[i], "version") == 0) {
			/* only add version if not NULL */
			if (argv[i] != NULL) {
				if (version->len > 0)
					g_string_append (version, ":");
				g_string_append (version, argv[i]);
			}
		} else if (g_strcmp0 (col_name[i], "release") == 0) {
			/* only add release if not NULL */
			if (argv[i] != NULL) {
				if (version->len > 0)
					g_string_append (version, "-");
				g_string_append (version, argv[i]);
			}
		} else if (g_strcmp0 (col_name[i], "flags") == 0) {
			zif_depend_set_flag (depend,
					     zif_md_primary_sql_flags_to_flag (argv[i]));
		} else {
			g_warning ("unrecognized: %s=%s", col_name[i], argv[i]);
		}
	}
	zif_depend_set_version (depend, version->str);
	g_ptr_array_add (array, depend);
	g_string_free (version, TRUE);
	return 0;
}

/**
 * zif_md_primary_sql_what_depends:
 **/
static GPtrArray *
zif_md_primary_sql_what_depends (ZifMd *md, const gchar *table_name, GPtrArray *depends,
				 ZifState *state, GError **error)
{
	GString *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;
	guint i, j;
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifState *state_local;
	ZifDepend *depend_tmp;
	ZifMdPrimarySqlData *data = NULL;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

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

	/* create one super huge statement with 'ORs' rather than doing
	 * thousands of indervidual queries */
	statement = g_string_new ("");
	g_string_append (statement, "BEGIN;\n");

	for (j=0; j<depends->len; j+= ZIF_MD_PRIMARY_SQL_MAX_EXPRESSION_DEPTH) {
		g_string_append_printf (statement, ZIF_MD_PRIMARY_SQL_HEADER ", %s depend WHERE "
					"p.pkgKey = depend.pkgKey AND (",
					table_name);

		for (i=j; i<depends->len && (i-j)<ZIF_MD_PRIMARY_SQL_MAX_EXPRESSION_DEPTH; i++) {
			depend_tmp = g_ptr_array_index (depends, i);
			g_string_append_printf (statement, "depend.name = '%s' OR ",
						zif_depend_get_name (depend_tmp));
		}
		/* remove trailing OR */
		g_string_set_size (statement, statement->len - 4);
		g_string_append (statement, ");\n");
	}

	/* a package always provides itself, even without an explicit provide */
	if (g_strcmp0 (table_name, "provides") == 0) {
		for (j=0; j<depends->len; j+= ZIF_MD_PRIMARY_SQL_MAX_EXPRESSION_DEPTH) {
			g_string_append (statement, ZIF_MD_PRIMARY_SQL_HEADER " WHERE ");

			for (i=j; i<depends->len && (i-j)<ZIF_MD_PRIMARY_SQL_MAX_EXPRESSION_DEPTH; i++) {
				depend_tmp = g_ptr_array_index (depends, i);
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
	rc = sqlite3_exec (md_primary_sql->priv->db, statement->str, zif_md_primary_sql_sqlite_create_package_cb, data, &error_msg);
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
	if (g_strcmp0 (table_name, "provides") == 0) {
		ret = zif_package_array_filter_provide (data->packages,
							depends,
							state_local,
							error);
	} else if (g_strcmp0 (table_name, "requires") == 0) {
		ret = zif_package_array_filter_require (data->packages,
							depends,
							state_local,
							error);
	} else if (g_strcmp0 (table_name, "obsoletes") == 0) {
		ret = zif_package_array_filter_obsolete (data->packages,
							 depends,
							 state_local,
							 error);
	} else if (g_strcmp0 (table_name, "conflicts") == 0) {
		ret = zif_package_array_filter_conflict (data->packages,
							 depends,
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
 * zif_md_primary_sql_get_provides:
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

