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

#include "dum-repo-md.h"
#include "dum-repo-md-master.h"
#include "dum-repo-md-primary.h"
#include "dum-package-remote.h"

#include "egg-debug.h"
#include "egg-string.h"

#define DUM_REPO_MD_PRIMARY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_REPO_MD_PRIMARY, DumRepoMdPrimaryPrivate))

struct DumRepoMdPrimaryPrivate
{
	gboolean		 loaded;
	sqlite3			*db;
};

typedef struct {
	const gchar		*id;
	GPtrArray		*packages;
} DumRepoMdPrimaryData;

G_DEFINE_TYPE (DumRepoMdPrimary, dum_repo_md_primary, DUM_TYPE_REPO_MD)

/**
 * dum_repo_md_primary_load:
 **/
static gboolean
dum_repo_md_primary_load (DumRepoMd *md, GError **error)
{
	const gchar *filename;
	gint rc;
	DumRepoMdPrimary *primary = DUM_REPO_MD_PRIMARY (md);

	g_return_val_if_fail (DUM_IS_REPO_MD_PRIMARY (md), FALSE);

	/* already loaded */
	if (primary->priv->loaded)
		goto out;

	/* get filename */
	filename = dum_repo_md_get_filename (md);
	if (filename == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get filename for primary");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);
	rc = sqlite3_open (filename, &primary->priv->db);
	if (rc != 0) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (primary->priv->db));
		if (error != NULL)
			*error = g_error_new (1, 0, "can't open database: %s", sqlite3_errmsg (primary->priv->db));
		goto out;
	}

	/* we don't need to keep syncing */
	sqlite3_exec (primary->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
	primary->priv->loaded = TRUE;
out:
	return primary->priv->loaded;
}

/**
 * dum_repo_md_primary_sqlite_create_package_cb:
 **/
static gint
dum_repo_md_primary_sqlite_create_package_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	DumRepoMdPrimaryData *fldata = (DumRepoMdPrimaryData *) data;
	DumPackageRemote *package;

	package = dum_package_remote_new ();
	dum_package_remote_set_from_repo (package, argc, col_name, argv, fldata->id, NULL);
	g_ptr_array_add (fldata->packages, package);

	return 0;
}

/**
 * dum_repo_md_primary_search:
 **/
static GPtrArray *
dum_repo_md_primary_search (DumRepoMdPrimary *md, const gchar *pred, GError **error)
{
	gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;
	gboolean ret;
	GError *error_local = NULL;
	DumRepoMdPrimaryData *data = NULL;
	GPtrArray *array = NULL;

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = dum_repo_md_load (DUM_REPO_MD (md), &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load repo_md_primary file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* create data struct we can pass to the callback */
	data = g_new0 (DumRepoMdPrimaryData, 1);
	data->id = dum_repo_md_get_id (DUM_REPO_MD (md));
	data->packages = g_ptr_array_new ();

	statement = g_strdup_printf ("SELECT pkgId, name, arch, version, "
				     "epoch, release, summary, description, url, "
				     "rpm_license, rpm_group, size_package, location_href FROM packages %s", pred);
	rc = sqlite3_exec (md->priv->db, statement, dum_repo_md_primary_sqlite_create_package_cb, data, &error_msg);
	if (rc != SQLITE_OK) {
		if (error != NULL)
			*error = g_error_new (1, 0, "SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		g_ptr_array_free (data->packages, TRUE);
		goto out;
	}
	/* list of packages */
	array = data->packages;
out:
	g_free (data);
	g_free (statement);
	return array;
}

/**
 * dum_repo_md_primary_resolve:
 **/
GPtrArray *
dum_repo_md_primary_resolve (DumRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (DUM_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE name = '%s'", search);
	array = dum_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * dum_repo_md_primary_search_name:
 **/
GPtrArray *
dum_repo_md_primary_search_name (DumRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (DUM_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE name LIKE '%%%s%%'", search);
	array = dum_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * dum_repo_md_primary_search_details:
 **/
GPtrArray *
dum_repo_md_primary_search_details (DumRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (DUM_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE name LIKE '%%%s%%' OR summary LIKE '%%%s%%' OR description LIKE '%%%s%%'", search, search, search);
	array = dum_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * dum_repo_md_primary_search_group:
 **/
GPtrArray *
dum_repo_md_primary_search_group (DumRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (DUM_IS_REPO_MD_PRIMARY (md), FALSE);

	/* FIXME: search with predicate */
	pred = g_strdup_printf ("WHERE group = '%s'", search);
	array = dum_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * dum_repo_md_primary_search_pkgid:
 **/
GPtrArray *
dum_repo_md_primary_search_pkgid (DumRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (DUM_IS_REPO_MD_PRIMARY (md), FALSE);

	/* FIXME: search with predicate */
	pred = g_strdup_printf ("WHERE pkgid = '%s'", search);
	array = dum_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * dum_repo_md_primary_find_package:
 **/
GPtrArray *
dum_repo_md_primary_find_package (DumRepoMdPrimary *md, const PkPackageId *id, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (DUM_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate, TODO: search version (epoch+release) */
	pred = g_strdup_printf ("WHERE name = '%s' AND arch = '%s'", id->name, id->arch);
	array = dum_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * dum_repo_md_primary_get_packages:
 **/
GPtrArray *
dum_repo_md_primary_get_packages (DumRepoMdPrimary *md, GError **error)
{
	GPtrArray *array;

	g_return_val_if_fail (DUM_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate */
	array = dum_repo_md_primary_search (md, "", error);
	return array;
}

/**
 * dum_repo_md_primary_finalize:
 **/
static void
dum_repo_md_primary_finalize (GObject *object)
{
	DumRepoMdPrimary *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_REPO_MD_PRIMARY (object));
	md = DUM_REPO_MD_PRIMARY (object);

	sqlite3_close (md->priv->db);

	G_OBJECT_CLASS (dum_repo_md_primary_parent_class)->finalize (object);
}

/**
 * dum_repo_md_primary_class_init:
 **/
static void
dum_repo_md_primary_class_init (DumRepoMdPrimaryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	DumRepoMdClass *repo_md_class = DUM_REPO_MD_CLASS (klass);
	object_class->finalize = dum_repo_md_primary_finalize;

	/* map */
	repo_md_class->load = dum_repo_md_primary_load;
	g_type_class_add_private (klass, sizeof (DumRepoMdPrimaryPrivate));
}

/**
 * dum_repo_md_primary_init:
 **/
static void
dum_repo_md_primary_init (DumRepoMdPrimary *md)
{
	md->priv = DUM_REPO_MD_PRIMARY_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->db = NULL;
}

/**
 * dum_repo_md_primary_new:
 * Return value: A new repo_md_primary class instance.
 **/
DumRepoMdPrimary *
dum_repo_md_primary_new (void)
{
	DumRepoMdPrimary *md;
	md = g_object_new (DUM_TYPE_REPO_MD_PRIMARY, NULL);
	return DUM_REPO_MD_PRIMARY (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_repo_md_primary_test (EggTest *test)
{
	DumRepoMdMaster *master;
	DumRepoMdPrimary *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	DumPackage *package;
	DumString *summary;
	const DumRepoMdInfoData *info_data;

	if (!egg_test_start (test, "DumRepoMdPrimary"))
		return;

	/************************************************************/
	egg_test_title (test, "get repo_md_primary md");
	md = dum_repo_md_primary_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "set cache dir");
	ret = dum_repo_md_set_cache_dir (DUM_REPO_MD (md), "../test/cache");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = dum_repo_md_set_id (DUM_REPO_MD (md), "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/* set all the data so we can load this */
	master = dum_repo_md_master_new ();
	dum_repo_md_set_cache_dir (DUM_REPO_MD (master), "../test/cache");
	dum_repo_md_set_id (DUM_REPO_MD (master), "fedora");
	info_data = dum_repo_md_master_get_info (master, DUM_REPO_MD_TYPE_PRIMARY, NULL);
	dum_repo_md_set_info_data (DUM_REPO_MD (md), info_data);

	/************************************************************/
	egg_test_title (test, "load");
	ret = dum_repo_md_load (DUM_REPO_MD (md), &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "search for files");
	array = dum_repo_md_primary_resolve (md, "gnome-power-manager", &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	egg_test_assert (test, array->len == 1);

	/************************************************************/
	egg_test_title (test, "correct value");
	package = g_ptr_array_index (array, 0);
	summary = dum_package_get_summary (package, NULL);
	if (g_strcmp0 (summary->value, "GNOME Power Manager") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct summary '%s'", summary->value);
	dum_string_unref (summary);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (md);
	g_object_unref (master);

	egg_test_end (test);
}
#endif

