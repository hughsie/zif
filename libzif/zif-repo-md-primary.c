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

/**
 * SECTION:zif-repo-md-primary
 * @short_description: Primary metadata functionality
 *
 * Provide access to the primary repo metadata.
 * This object is a subclass of #ZifRepoMd
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
#include "zif-repo-md-primary.h"
#include "zif-package-remote.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_REPO_MD_PRIMARY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPO_MD_PRIMARY, ZifRepoMdPrimaryPrivate))

/**
 * ZifRepoMdPrimaryPrivate:
 *
 * Private #ZifRepoMdPrimary data
 **/
struct _ZifRepoMdPrimaryPrivate
{
	gboolean		 loaded;
	sqlite3			*db;
};

typedef struct {
	const gchar		*id;
	GPtrArray		*packages;
} ZifRepoMdPrimaryData;

G_DEFINE_TYPE (ZifRepoMdPrimary, zif_repo_md_primary, ZIF_TYPE_REPO_MD)

/**
 * zif_repo_md_primary_unload:
 **/
static gboolean
zif_repo_md_primary_unload (ZifRepoMd *md, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_repo_md_primary_load:
 **/
static gboolean
zif_repo_md_primary_load (ZifRepoMd *md, GError **error)
{
	const gchar *filename;
	gint rc;
	ZifRepoMdPrimary *primary = ZIF_REPO_MD_PRIMARY (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD_PRIMARY (md), FALSE);

	/* already loaded */
	if (primary->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_repo_md_get_filename_uncompressed (md);
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
 * zif_repo_md_primary_sqlite_create_package_cb:
 **/
static gint
zif_repo_md_primary_sqlite_create_package_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	ZifRepoMdPrimaryData *fldata = (ZifRepoMdPrimaryData *) data;
	ZifPackageRemote *package;

	package = zif_package_remote_new ();
	zif_package_remote_set_from_repo (package, argc, col_name, argv, fldata->id, NULL);
	g_ptr_array_add (fldata->packages, package);

	return 0;
}

/**
 * zif_repo_md_primary_search:
 **/
static GPtrArray *
zif_repo_md_primary_search (ZifRepoMdPrimary *md, const gchar *pred, GError **error)
{
	gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;
	gboolean ret;
	GError *error_local = NULL;
	ZifRepoMdPrimaryData *data = NULL;
	GPtrArray *array = NULL;

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_repo_md_load (ZIF_REPO_MD (md), &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load repo_md_primary file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* create data struct we can pass to the callback */
	data = g_new0 (ZifRepoMdPrimaryData, 1);
	data->id = zif_repo_md_get_id (ZIF_REPO_MD (md));
	data->packages = g_ptr_array_new ();

	statement = g_strdup_printf ("SELECT pkgId, name, arch, version, "
				     "epoch, release, summary, description, url, "
				     "rpm_license, rpm_group, size_package, location_href FROM packages %s", pred);
	rc = sqlite3_exec (md->priv->db, statement, zif_repo_md_primary_sqlite_create_package_cb, data, &error_msg);
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
 * zif_repo_md_primary_resolve:
 * @md: the #ZifRepoMdPrimary object
 * @search: the search term, e.g. "gnome-power-manager"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all remote packages that match the name exactly.
 *
 * Return value: an array of #ZifPackageRemote's
 **/
GPtrArray *
zif_repo_md_primary_resolve (ZifRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE name = '%s'", search);
	array = zif_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * zif_repo_md_primary_search_name:
 * @md: the #ZifRepoMdPrimary object
 * @search: the search term, e.g. "power"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the name.
 *
 * Return value: an array of #ZifPackageRemote's
 **/
GPtrArray *
zif_repo_md_primary_search_name (ZifRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE name LIKE '%%%s%%'", search);
	array = zif_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * zif_repo_md_primary_search_details:
 * @md: the #ZifRepoMdPrimary object
 * @search: the search term, e.g. "advanced"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the name or description.
 *
 * Return value: an array of #ZifPackageRemote's
 **/
GPtrArray *
zif_repo_md_primary_search_details (ZifRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE name LIKE '%%%s%%' OR summary LIKE '%%%s%%' OR description LIKE '%%%s%%'", search, search, search);
	array = zif_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * zif_repo_md_primary_search_group:
 * @md: the #ZifRepoMdPrimary object
 * @search: the search term, e.g. "games/console"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the group.
 *
 * Return value: an array of #ZifPackageRemote's
 **/
GPtrArray *
zif_repo_md_primary_search_group (ZifRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_REPO_MD_PRIMARY (md), FALSE);

	/* FIXME: search with predicate */
	pred = g_strdup_printf ("WHERE group = '%s'", search);
	array = zif_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * zif_repo_md_primary_search_pkgid:
 * @md: the #ZifRepoMdPrimary object
 * @search: the search term as a 64 bit hash
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the given pkgId.
 *
 * Return value: an array of #ZifPackageRemote's
 **/
GPtrArray *
zif_repo_md_primary_search_pkgid (ZifRepoMdPrimary *md, const gchar *search, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_REPO_MD_PRIMARY (md), FALSE);

	/* FIXME: search with predicate */
	pred = g_strdup_printf ("WHERE pkgid = '%s'", search);
	array = zif_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * zif_repo_md_primary_find_package:
 * @md: the #ZifRepoMdPrimary object
 * @id: the #PkPackageId to match
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match #PkPackageId.
 *
 * Return value: an array of #ZifPackageRemote's
 **/
GPtrArray *
zif_repo_md_primary_find_package (ZifRepoMdPrimary *md, const PkPackageId *id, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate, TODO: search version (epoch+release) */
	pred = g_strdup_printf ("WHERE name = '%s' AND arch = '%s'", id->name, id->arch);
	array = zif_repo_md_primary_search (md, pred, error);
	g_free (pred);

	return array;
}

/**
 * zif_repo_md_primary_get_packages:
 * @md: the #ZifRepoMdPrimary object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Returns all packages in the repo.
 *
 * Return value: an array of #ZifPackageRemote's
 **/
GPtrArray *
zif_repo_md_primary_get_packages (ZifRepoMdPrimary *md, GError **error)
{
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_REPO_MD_PRIMARY (md), FALSE);

	/* search with predicate */
	array = zif_repo_md_primary_search (md, "", error);
	return array;
}

/**
 * zif_repo_md_primary_finalize:
 **/
static void
zif_repo_md_primary_finalize (GObject *object)
{
	ZifRepoMdPrimary *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_REPO_MD_PRIMARY (object));
	md = ZIF_REPO_MD_PRIMARY (object);

	sqlite3_close (md->priv->db);

	G_OBJECT_CLASS (zif_repo_md_primary_parent_class)->finalize (object);
}

/**
 * zif_repo_md_primary_class_init:
 **/
static void
zif_repo_md_primary_class_init (ZifRepoMdPrimaryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifRepoMdClass *repo_md_class = ZIF_REPO_MD_CLASS (klass);
	object_class->finalize = zif_repo_md_primary_finalize;

	/* map */
	repo_md_class->load = zif_repo_md_primary_load;
	repo_md_class->unload = zif_repo_md_primary_unload;
	g_type_class_add_private (klass, sizeof (ZifRepoMdPrimaryPrivate));
}

/**
 * zif_repo_md_primary_init:
 **/
static void
zif_repo_md_primary_init (ZifRepoMdPrimary *md)
{
	md->priv = ZIF_REPO_MD_PRIMARY_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->db = NULL;
}

/**
 * zif_repo_md_primary_new:
 *
 * Return value: A new #ZifRepoMdPrimary class instance.
 **/
ZifRepoMdPrimary *
zif_repo_md_primary_new (void)
{
	ZifRepoMdPrimary *md;
	md = g_object_new (ZIF_TYPE_REPO_MD_PRIMARY, NULL);
	return ZIF_REPO_MD_PRIMARY (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_repo_md_primary_test (EggTest *test)
{
	ZifRepoMdPrimary *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifPackage *package;
	ZifString *summary;

	if (!egg_test_start (test, "ZifRepoMdPrimary"))
		return;

	/************************************************************/
	egg_test_title (test, "get repo_md_primary md");
	md = zif_repo_md_primary_new ();
	egg_test_assert (test, md != NULL);

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

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_repo_md_set_filename (ZIF_REPO_MD (md), "../test/cache/fedora/35d817e2bac701525fa72cec57387a2e3457bf32642adeee1e345cc180044c86-primary.sqlite");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

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
	array = zif_repo_md_primary_resolve (md, "gnome-power-manager", &error);
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
	summary = zif_package_get_summary (package, NULL);
	if (g_strcmp0 (zif_string_get_value (summary), "GNOME Power Manager") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct summary '%s'", zif_string_get_value (summary));
	zif_string_unref (summary);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (md);

	egg_test_end (test);
}
#endif

