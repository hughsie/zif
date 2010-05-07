/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-md-filelists-sql
 * @short_description: File list metadata functionality
 *
 * Provide access to the file list metadata.
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
#include "zif-md-filelists-sql.h"
#include "zif-package-remote.h"

#include "egg-debug.h"

#define ZIF_MD_FILELISTS_SQL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_FILELISTS_SQL, ZifMdFilelistsSqlPrivate))

/**
 * ZifMdFilelistsSqlPrivate:
 *
 * Private #ZifMdFilelistsSql data
 **/
struct _ZifMdFilelistsSqlPrivate
{
	gboolean		 loaded;
	sqlite3			*db;
};

typedef struct {
	gchar			*filename;
	GPtrArray		*array;
} ZifMdFilelistsSqlData;

G_DEFINE_TYPE (ZifMdFilelistsSql, zif_md_filelists_sql, ZIF_TYPE_MD)

/**
 * zif_md_filelists_sql_unload:
 **/
static gboolean
zif_md_filelists_sql_unload (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_filelists_sql_load:
 **/
static gboolean
zif_md_filelists_sql_load (ZifMd *md, ZifState *state, GError **error)
{
	const gchar *filename;
	gint rc;
	ZifMdFilelistsSql *filelists = ZIF_MD_FILELISTS_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_FILELISTS_SQL (md), FALSE);

	/* already loaded */
	if (filelists->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for filelists");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);
	rc = sqlite3_open (filename, &filelists->priv->db);
	if (rc != 0) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (filelists->priv->db));
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "can't open database: %s", sqlite3_errmsg (filelists->priv->db));
		goto out;
	}

	/* we don't need to keep syncing */
	sqlite3_exec (filelists->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
	filelists->priv->loaded = TRUE;
out:
	return filelists->priv->loaded;
}

/**
 * zif_md_filelists_sql_sqlite_get_id_cb:
 **/
static gint
zif_md_filelists_sql_sqlite_get_id_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gchar **pkgid = (gchar **) data;
	*pkgid = g_strdup (argv[0]);
	return 0;
}

/**
 * zif_md_filelists_sql_sqlite_get_pkgkey_cb:
 **/
static gint
zif_md_filelists_sql_sqlite_get_pkgkey_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint i;
	gchar **filenames = NULL;
	gchar **filenames_r = NULL;
	gchar **id_r = NULL;
	ZifMdFilelistsSqlData *fldata = (ZifMdFilelistsSqlData *) data;

	/* get pointers to the arguments */
	for (i=0;i<argc;i++) {
		if (g_strcmp0 (col_name[i], "pkgKey") == 0)
			id_r = &argv[i];
		else if (g_strcmp0 (col_name[i], "filenames") == 0)
			filenames_r = &argv[i];
	}

	/* either is undereferencable */
	if (filenames_r == NULL || id_r == NULL) {
		egg_warning ("no file data");
		goto out;
	}

	/* split the filenames */
	filenames = g_strsplit (*filenames_r, "/", -1);
	for (i=0; filenames[i] != NULL ;i++) {
		/* do we match */
		if (g_strcmp0 (fldata->filename, filenames[i]) == 0) {
			egg_debug ("found %s for %s", filenames[i], *id_r);
			g_ptr_array_add (fldata->array, GUINT_TO_POINTER (atoi (*id_r)));
		}
	}
out:
	g_strfreev (filenames);
	return 0;
}

/**
 * zif_md_filelists_sql_sqlite_get_files_cb:
 **/
static gint
zif_md_filelists_sql_sqlite_get_files_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint i;
	gchar **filename = NULL;
	gchar **dirname = NULL;
	GPtrArray **array = (GPtrArray **) data;

	/* get pointers to the arguments */
	for (i=0;i<argc;i++) {
		if (g_strcmp0 (col_name[i], "filenames") == 0)
			filename = &argv[i];
		else if (g_strcmp0 (col_name[i], "dirname") == 0)
			dirname = &argv[i];
	}

	/* check for invalid entries */
	if (filename == NULL || dirname == NULL) {
		egg_warning ("failed on %p, %p", filename, dirname);
		return 0;
	}

	/* add complete path */
	g_ptr_array_add (*array, g_strdup (g_build_filename (*dirname, *filename, NULL)));
	return 0;
}

/**
 * zif_md_filelists_sql_get_files:
 **/
static GPtrArray *
zif_md_filelists_sql_get_files (ZifMd *md, ZifPackage *package,
				ZifState *state, GError **error)
{
	gchar *statement = NULL;
	gint rc;
	gboolean ret;
	const gchar *pkgid;
	gchar *pkgkey = NULL;
	GError *error_local = NULL;
	gchar *error_msg = NULL;
	GPtrArray *array = NULL;
	GPtrArray *files = NULL;
	ZifMdFilelistsSql *md_filelists_sql = ZIF_MD_FILELISTS_SQL (md);

	/* if not already loaded, load */
	if (!md_filelists_sql->priv->loaded) {
		ret = zif_md_load (md, state, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get pkgkey from pkgid */
	pkgid = zif_package_remote_get_pkgid (ZIF_PACKAGE_REMOTE (package));
	statement = g_strdup_printf ("SELECT pkgKey FROM packages WHERE pkgId = '%s' LIMIT 1", pkgid);
	rc = sqlite3_exec (md_filelists_sql->priv->db, statement, zif_md_filelists_sql_sqlite_get_id_cb, &pkgkey, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error (failed to get packages): %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* failed */
	if (pkgkey == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "failed to get pkgkey for %s", pkgid);
		goto out;
	}

	/* get files for pkgkey */
	files = g_ptr_array_new_with_free_func (g_free);
	statement = g_strdup_printf ("SELECT dirname, filenames FROM filelist WHERE pkgKey = '%s'", pkgkey);
	rc = sqlite3_exec (md_filelists_sql->priv->db, statement, zif_md_filelists_sql_sqlite_get_files_cb, &files, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error (failed to get packages): %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* success */
	array = g_ptr_array_ref (files);
out:
	if (files != NULL)
		g_ptr_array_unref (files);
	return array;
}

/**
 * zif_md_filelists_sql_search_file:
 **/
static GPtrArray *
zif_md_filelists_sql_search_file (ZifMd *md, gchar **search,
				  ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;
	gboolean ret;
	guint i;
	GError *error_local = NULL;
	gchar *filename = NULL;
	gchar *dirname = NULL;
	ZifMdFilelistsSql *md_filelists_sql = ZIF_MD_FILELISTS_SQL (md);
	ZifMdFilelistsSqlData *data = NULL;

	g_return_val_if_fail (ZIF_IS_MD_FILELISTS_SQL (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!md_filelists_sql->priv->loaded) {
		ret = zif_md_load (md, state, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* split the search term into directory and filename */
	dirname = g_path_get_dirname (search[0]);
	filename = g_path_get_basename (search[0]);
	egg_debug ("dirname=%s, filename=%s", dirname, filename);

	/* create data struct we can pass to the callback */
	data = g_new0 (ZifMdFilelistsSqlData, 1);
	data->filename = g_path_get_basename (search[0]);
	data->array = g_ptr_array_new ();

	/* populate _array with guint pkgKey */
	statement = g_strdup_printf ("SELECT filenames, pkgKey FROM filelist WHERE dirname = '%s'", dirname);
	rc = sqlite3_exec (md_filelists_sql->priv->db, statement, zif_md_filelists_sql_sqlite_get_pkgkey_cb, data, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error (failed to get keys): %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* convert each pkgKey */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	for (i=0; i<data->array->len; i++) {
		guint key;
		gchar *pkgid = NULL;

		/* convert the pkgKey to a pkgId */
		key = GPOINTER_TO_UINT (g_ptr_array_index (data->array, i));
		statement = g_strdup_printf ("SELECT pkgId FROM packages WHERE pkgKey = %i LIMIT 1", key);
		rc = sqlite3_exec (md_filelists_sql->priv->db, statement, zif_md_filelists_sql_sqlite_get_id_cb, &pkgid, &error_msg);
		g_free (statement);
		if (rc != SQLITE_OK) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
				     "SQL error (failed to get packages): %s", error_msg);
			sqlite3_free (error_msg);
			goto out;
		}

		/* we failed to get any results */
		if (pkgid == NULL) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
				     "failed to resolve pkgKey: %i", key);
			goto out;
		}

		/* added to tracked array, so no need to free pkgid */
		g_ptr_array_add (array, pkgid);
	}
out:
	if (data != NULL) {
		g_free (data->filename);
		g_ptr_array_unref (data->array);
		g_free (data);
	}
	g_free (dirname);
	g_free (filename);
	return array;
}

/**
 * zif_md_filelists_sql_finalize:
 **/
static void
zif_md_filelists_sql_finalize (GObject *object)
{
	ZifMdFilelistsSql *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_FILELISTS_SQL (object));
	md = ZIF_MD_FILELISTS_SQL (object);

	sqlite3_close (md->priv->db);

	G_OBJECT_CLASS (zif_md_filelists_sql_parent_class)->finalize (object);
}

/**
 * zif_md_filelists_sql_class_init:
 **/
static void
zif_md_filelists_sql_class_init (ZifMdFilelistsSqlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_filelists_sql_finalize;

	/* map */
	md_class->load = zif_md_filelists_sql_load;
	md_class->unload = zif_md_filelists_sql_unload;
	md_class->search_file = zif_md_filelists_sql_search_file;
	md_class->get_files = zif_md_filelists_sql_get_files;
	g_type_class_add_private (klass, sizeof (ZifMdFilelistsSqlPrivate));
}

/**
 * zif_md_filelists_sql_init:
 **/
static void
zif_md_filelists_sql_init (ZifMdFilelistsSql *md)
{
	md->priv = ZIF_MD_FILELISTS_SQL_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->db = NULL;
}

/**
 * zif_md_filelists_sql_new:
 *
 * Return value: A new #ZifMdFilelistsSql class instance.
 *
 * Since: 0.0.1
 **/
ZifMdFilelistsSql *
zif_md_filelists_sql_new (void)
{
	ZifMdFilelistsSql *md;
	md = g_object_new (ZIF_TYPE_MD_FILELISTS_SQL, NULL);
	return ZIF_MD_FILELISTS_SQL (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_md_filelists_sql_test (EggTest *test)
{
	ZifMdFilelistsSql *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *pkgid;
	GCancellable *cancellable;
	ZifState *state;
	const gchar *data[] = { "/usr/bin/gnome-power-manager", NULL };

	if (!egg_test_start (test, "ZifMdFilelistsSql"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	state = zif_state_new ();

	/************************************************************/
	egg_test_title (test, "get store_remote md");
	md = zif_md_filelists_sql_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = zif_md_set_id (ZIF_MD (md), "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set type");
	ret = zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_FILELISTS_SQL);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum type");
	ret = zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum compressed");
	ret = zif_md_set_checksum (ZIF_MD (md), "e00e88a8b6eee3798544764b6fe31ef8c9d071a824177c7cdc4fe749289198a9");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum uncompressed");
	ret = zif_md_set_checksum_uncompressed (ZIF_MD (md), "2b4336cb43e75610662bc0b3a362ca4cb7ba874528735a27c0d55148c3901792");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_md_set_filename (ZIF_MD (md), "../test/cache/fedora/e00e88a8b6eee3798544764b6fe31ef8c9d071a824177c7cdc4fe749289198a9-filelists.sqlite.bz2");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_md_load (ZIF_MD (md), state, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "search for files");
	array = zif_md_filelists_sql_search_file (ZIF_MD (md), (gchar**)data, state, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	egg_test_assert (test, array->len == 1);

	/************************************************************/
	egg_test_title (test, "correct value");
	pkgid = g_ptr_array_index (array, 0);
	if (pkgid[0] != '\0' && strlen (pkgid) == 64)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get a correct pkgId '%s' (%i)", pkgid, strlen (pkgid));
	g_ptr_array_unref (array);

	g_object_unref (md);
	g_object_unref (cancellable);
	g_object_unref (state);

	egg_test_end (test);
}
#endif

