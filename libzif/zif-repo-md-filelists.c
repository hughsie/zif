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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <gio/gio.h>

#include "zif-repo-md.h"
#include "zif-repo-md-master.h"
#include "zif-repo-md-filelists.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_REPO_MD_FILELISTS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPO_MD_FILELISTS, ZifRepoMdFilelistsPrivate))

struct ZifRepoMdFilelistsPrivate
{
	gboolean		 loaded;
	sqlite3			*db;
};

typedef struct {
	gchar			*filename;
	GPtrArray		*array;
} ZifRepoMdFilelistsData;

G_DEFINE_TYPE (ZifRepoMdFilelists, zif_repo_md_filelists, ZIF_TYPE_REPO_MD)

/**
 * zif_repo_md_filelists_clean:
 **/
static gboolean
zif_repo_md_filelists_clean (ZifRepoMd *md, GError **error)
{
	gboolean ret = FALSE;
	gboolean exists;
	const gchar *filename;
	GFile *file;
	GError *error_local = NULL;

	/* get filename */
	filename = zif_repo_md_get_filename (md);
	if (filename == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get filename for filelists");
		goto out;
	}

	/* file does not exist */
	exists = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (exists) {
		file = g_file_new_for_path (filename);
		ret = g_file_delete (file, NULL, &error_local);
		g_object_unref (file);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to delete metadata file %s: %s", filename, error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* okay */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_repo_md_filelists_load:
 **/
static gboolean
zif_repo_md_filelists_load (ZifRepoMd *md, GError **error)
{
	const gchar *filename;
	gint rc;
	ZifRepoMdFilelists *filelists = ZIF_REPO_MD_FILELISTS (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD_FILELISTS (md), FALSE);

	/* already loaded */
	if (filelists->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_repo_md_get_filename (md);
	if (filename == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get filename for filelists");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);
	rc = sqlite3_open (filename, &filelists->priv->db);
	if (rc != 0) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (filelists->priv->db));
		if (error != NULL)
			*error = g_error_new (1, 0, "can't open database: %s", sqlite3_errmsg (filelists->priv->db));
		goto out;
	}

	/* we don't need to keep syncing */
	sqlite3_exec (filelists->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
	filelists->priv->loaded = TRUE;
out:
	return filelists->priv->loaded;
}

/**
 * zif_store_remote_sqlite_get_id_cb:
 **/
static gint
zif_repo_md_filelists_sqlite_get_id_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gchar **pkgid = (gchar **) data;
	*pkgid = g_strdup (argv[0]);
	return 0;
}

/**
 * zif_repo_md_filelists_sqlite_get_files_cb:
 **/
static gint
zif_repo_md_filelists_sqlite_get_files_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint i;
	gchar **filenames = NULL;
	gchar **filenames_r = NULL;
	gchar **id_r = NULL;
	ZifRepoMdFilelistsData *fldata = (ZifRepoMdFilelistsData *) data;

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
 * zif_repo_md_filelists_search_file:
 *
 * CREATE TABLE db_info (dbversion INTEGER, checksum TEXT);
 * INSERT INTO "db_info" VALUES(10,'34d42ab6240fbd8d5fd464fafea49c09f7acc93a');
 * CREATE TABLE packages (  pkgKey INTEGER PRIMARY KEY,  pkgId TEXT);
 * INSERT INTO "packages" VALUES(1,'1f2be47c69ac5f9a6de6b9b50762d48c867d654a');
 * CREATE TABLE filelist (  pkgKey INTEGER,  dirname TEXT,  filenames TEXT,  filetypes TEXT);
 * INSERT INTO "filelist" VALUES(1,'/usr/share/man/man1','metaflac.1.gz/flac.1.gz','ff');
 *
 * Returns a string list of pkgId's
 **/
GPtrArray *
zif_repo_md_filelists_search_file (ZifRepoMdFilelists *md, const gchar *search, GError **error)
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
	ZifRepoMdFilelistsData *data = NULL;

	g_return_val_if_fail (ZIF_IS_REPO_MD_FILELISTS (md), FALSE);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_repo_md_filelists_load (ZIF_REPO_MD (md), &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* split the search term into directory and filename */
	dirname = g_path_get_dirname (search);
	filename = g_path_get_basename (search);
	egg_debug ("dirname=%s, filename=%s", dirname, filename);

	/* create data struct we can pass to the callback */
	data = g_new0 (ZifRepoMdFilelistsData, 1);
	data->filename = g_path_get_basename (search);
	data->array = g_ptr_array_new ();

	/* populate _array with guint pkgKey */
	statement = g_strdup_printf ("SELECT filenames, pkgKey FROM filelist WHERE dirname = '%s'", dirname);
	rc = sqlite3_exec (md->priv->db, statement, zif_repo_md_filelists_sqlite_get_files_cb, data, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		if (error != NULL)
			*error = g_error_new (1, 0, "SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* convert each pkgKey */
	array = g_ptr_array_new ();
	for (i=0; i<data->array->len; i++) {
		guint key;
		gchar *pkgid = NULL;

		/* convert the pkgKey to a pkgId */
		key = GPOINTER_TO_UINT (g_ptr_array_index (data->array, i));
		statement = g_strdup_printf ("SELECT pkgId FROM packages WHERE pkgKey = %i LIMIT 1", key);
		rc = sqlite3_exec (md->priv->db, statement, zif_repo_md_filelists_sqlite_get_id_cb, &pkgid, &error_msg);
		g_free (statement);
		if (rc != SQLITE_OK) {
			if (error != NULL)
				*error = g_error_new (1, 0, "SQL error: %s", error_msg);
			sqlite3_free (error_msg);
			goto out;
		}

		/* we failed to get any results */
		if (pkgid == NULL) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to resolve pkgKey: %i", key);
			goto out;
		}

		/* added to tracked array, so no need to free pkgid */
		g_ptr_array_add (array, pkgid);
	}
out:
	if (data != NULL) {
		g_free (data->filename);
		g_ptr_array_free (data->array, TRUE);
		g_free (data);
	}
	g_free (dirname);
	g_free (filename);
	return array;
}

/**
 * zif_repo_md_filelists_finalize:
 **/
static void
zif_repo_md_filelists_finalize (GObject *object)
{
	ZifRepoMdFilelists *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_REPO_MD_FILELISTS (object));
	md = ZIF_REPO_MD_FILELISTS (object);

	sqlite3_close (md->priv->db);

	G_OBJECT_CLASS (zif_repo_md_filelists_parent_class)->finalize (object);
}

/**
 * zif_repo_md_filelists_class_init:
 **/
static void
zif_repo_md_filelists_class_init (ZifRepoMdFilelistsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifRepoMdClass *repo_md_class = ZIF_REPO_MD_CLASS (klass);
	object_class->finalize = zif_repo_md_filelists_finalize;

	/* map */
	repo_md_class->load = zif_repo_md_filelists_load;
	repo_md_class->clean = zif_repo_md_filelists_clean;
	g_type_class_add_private (klass, sizeof (ZifRepoMdFilelistsPrivate));
}

/**
 * zif_repo_md_filelists_init:
 **/
static void
zif_repo_md_filelists_init (ZifRepoMdFilelists *md)
{
	md->priv = ZIF_REPO_MD_FILELISTS_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->db = NULL;
}

/**
 * zif_repo_md_filelists_new:
 * Return value: A new repo_md_filelists class instance.
 **/
ZifRepoMdFilelists *
zif_repo_md_filelists_new (void)
{
	ZifRepoMdFilelists *md;
	md = g_object_new (ZIF_TYPE_REPO_MD_FILELISTS, NULL);
	return ZIF_REPO_MD_FILELISTS (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_repo_md_filelists_test (EggTest *test)
{
	ZifRepoMdMaster *master;
	ZifRepoMdFilelists *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *pkgid;
	const ZifRepoMdInfoData *info_data;

	if (!egg_test_start (test, "ZifRepoMdFilelists"))
		return;

	/************************************************************/
	egg_test_title (test, "get store_remote md");
	md = zif_repo_md_filelists_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "set cache dir");
	ret = zif_repo_md_set_cache_dir (ZIF_REPO_MD (md), "../test/cache");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = zif_repo_md_set_id (ZIF_REPO_MD (md), "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/* set all the data so we can load this */
	master = zif_repo_md_master_new ();
	zif_repo_md_set_cache_dir (ZIF_REPO_MD (master), "../test/cache");
	zif_repo_md_set_id (ZIF_REPO_MD (master), "fedora");
	info_data = zif_repo_md_master_get_info (master, ZIF_REPO_MD_TYPE_FILELISTS, NULL);
	zif_repo_md_set_info_data (ZIF_REPO_MD (md), info_data);

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_repo_md_load (ZIF_REPO_MD (md), &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "search for files");
	array = zif_repo_md_filelists_search_file (md, "/usr/bin/gnome-power-manager", &error);
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
	if (g_strcmp0 (pkgid, "58c14cc4a690e9464a13c74bcd57724878870ddd") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct pkgId '%s'", pkgid);

	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (md);
	g_object_unref (master);

	egg_test_end (test);
}
#endif

