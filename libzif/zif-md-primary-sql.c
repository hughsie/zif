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
 * @short_description: Primary metadata functionality
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
#include "zif-package-remote.h"
#include "zif-utils.h"

#define ZIF_MD_PRIMARY_SQL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_PRIMARY_SQL, ZifMdPrimarySqlPrivate))

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
	ZifPackageRemote *package;
	ZifStoreRemote *store_remote;
	gboolean ret;

	package = zif_package_remote_new ();
	store_remote = zif_md_get_store_remote (ZIF_MD (fldata->md));
	if (store_remote != NULL) {
		/* this is not set in a test harness */
		zif_package_remote_set_store_remote (package, store_remote);
	} else {
		g_debug ("no remote store for %s, which is okay as we're in make check", argv[1]);
	}

	/* add */
	ret = zif_package_remote_set_from_repo (package, argc, col_name, argv, fldata->id, NULL);
	if (ret) {
		g_ptr_array_add (fldata->packages, package);
	} else {
		g_warning ("failed to add: %s", argv[1]);
		g_object_unref (package);
	}

	return 0;
}

#define ZIF_MD_PRIMARY_SQL_HEADER "SELECT pkgId, name, arch, version, " \
				  "epoch, release, summary, description, url, " \
				  "rpm_license, rpm_group, size_package, location_href, "\
				  "time_file FROM packages"

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
			     "SQL error: %s\n", error_msg);
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
	statement = zif_md_primary_sql_get_statement_for_pred ("name = '###'", search);
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
	statement = zif_md_primary_sql_get_statement_for_pred ("name LIKE '%%###%%'", search);
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
	statement = zif_md_primary_sql_get_statement_for_pred ("name LIKE '%%###%%' OR "
							       "summary LIKE '%%###%%' OR "
							       "description LIKE '%%###%%'", search);
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
	statement = zif_md_primary_sql_get_statement_for_pred ("rpm_group = '###'", search);
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
	statement = zif_md_primary_sql_get_statement_for_pred ("pkgid = '###'", search);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_search_pkgkey:
 **/
static GPtrArray *
zif_md_primary_sql_search_pkgkey (ZifMd *md, guint pkgkey,
				  ZifState *state, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	statement = g_strdup_printf (ZIF_MD_PRIMARY_SQL_HEADER " WHERE pkgKey = '%i'", pkgkey);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
	g_free (statement);
	return array;
}

/**
 * zif_md_primary_sql_sqlite_pkgkey_cb:
 **/
static gint
zif_md_primary_sql_sqlite_pkgkey_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint i;
	guint pkgkey;
	gchar *endptr = NULL;
	GPtrArray *array = (GPtrArray *) data;

	/* get the ID */
	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "pkgKey") == 0) {
			pkgkey = g_ascii_strtoull (argv[i], &endptr, 10);
			if (argv[i] == endptr)
				g_warning ("failed to parse pkgKey %s", argv[i]);
			else
				g_ptr_array_add (array, GUINT_TO_POINTER (pkgkey));
		} else {
			g_warning ("unrecognized: %s=%s", col_name[i], argv[i]);
		}
	}
	return 0;
}

/**
 * zif_md_primary_sql_what_provides:
 **/
static GPtrArray *
zif_md_primary_sql_what_provides (ZifMd *md, gchar **search,
				  ZifState *state, GError **error)
{
	gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	GPtrArray *pkgkey_array = NULL;
	guint i;
	guint pkgkey;
	ZifState *state_local;
	ZifState *state_loop;
	ZifPackage *package;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (md_primary_sql->priv->loaded)
		zif_state_set_number_steps (state, 2);
	else
		zif_state_set_number_steps (state, 3);

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

	/* create data struct we can pass to the callback */
	pkgkey_array = g_ptr_array_new ();
	statement = g_strdup_printf ("SELECT pkgKey FROM provides WHERE name = '%s'", search[0]);
	rc = sqlite3_exec (md_primary_sql->priv->db, statement, zif_md_primary_sql_sqlite_pkgkey_cb, pkgkey_array, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* output array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* resolve each pkgkey to a package */
	state_local = zif_state_get_child (state);
	if (pkgkey_array->len > 0)
		zif_state_set_number_steps (state_local, pkgkey_array->len);
	for (i=0; i<pkgkey_array->len; i++) {
		pkgkey = GPOINTER_TO_UINT (g_ptr_array_index (pkgkey_array, i));

		/* get packages for pkgKey */
		state_loop = zif_state_get_child (state_local);
		array_tmp = zif_md_primary_sql_search_pkgkey (md, pkgkey, state_loop, error);
		if (array_tmp == NULL) {
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		/* check we only got one result */
		if (array_tmp->len == 0) {
			g_warning ("no package for pkgKey %i", pkgkey);
		} else if (array_tmp->len > 1 || array_tmp->len == 0) {
			g_warning ("more than one package for pkgKey %i", pkgkey);
		} else {
			package = g_ptr_array_index (array_tmp, 0);
			g_ptr_array_add (array, g_object_ref (package));
		}

		/* clear array */
		g_ptr_array_unref (array_tmp);

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_free (statement);
	if (pkgkey_array != NULL)
		g_ptr_array_unref (pkgkey_array);
	return array;
}

/**
 * zif_md_primary_sql_find_package:
 **/
static GPtrArray *
zif_md_primary_sql_find_package (ZifMd *md, const gchar *package_id, ZifState *state, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	gchar **split;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate, TODO: search version (epoch+release) */
	split = zif_package_id_split (package_id);
	statement = g_strdup_printf (ZIF_MD_PRIMARY_SQL_HEADER " WHERE name = '%s' AND arch = '%s'",
				     split[ZIF_PACKAGE_ID_NAME], split[ZIF_PACKAGE_ID_ARCH]);
	array = zif_md_primary_sql_search (md_primary_sql, statement, state, error);
	g_free (statement);
	g_strfreev (split);

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
 * Return value: A new #ZifMdPrimarySql class instance.
 *
 * Since: 0.1.0
 **/
ZifMd *
zif_md_primary_sql_new (void)
{
	ZifMdPrimarySql *md;
	md = g_object_new (ZIF_TYPE_MD_PRIMARY_SQL, NULL);
	return ZIF_MD (md);
}

