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
 * SECTION:zif-md-primary
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
#include "zif-md-primary.h"
#include "zif-package-remote.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_MD_PRIMARY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_PRIMARY, ZifMdPrimaryPrivate))

/**
 * ZifMdPrimaryPrivate:
 *
 * Private #ZifMdPrimary data
 **/
struct _ZifMdPrimaryPrivate
{
	gboolean		 loaded;
	sqlite3			*db;
};

typedef struct {
	const gchar		*id;
	GPtrArray		*packages;
} ZifMdPrimaryData;

G_DEFINE_TYPE (ZifMdPrimary, zif_md_primary, ZIF_TYPE_MD)

/**
 * zif_md_primary_unload:
 **/
static gboolean
zif_md_primary_unload (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_primary_load:
 **/
static gboolean
zif_md_primary_load (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	const gchar *filename;
	gint rc;
	ZifMdPrimary *primary = ZIF_MD_PRIMARY (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY (md), FALSE);

	/* already loaded */
	if (primary->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for primary");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);
	rc = sqlite3_open (filename, &primary->priv->db);
	if (rc != 0) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (primary->priv->db));
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "can't open database: %s", sqlite3_errmsg (primary->priv->db));
		goto out;
	}

	/* we don't need to keep syncing */
	sqlite3_exec (primary->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
	primary->priv->loaded = TRUE;
out:
	return primary->priv->loaded;
}

/**
 * zif_md_primary_sqlite_create_package_cb:
 **/
static gint
zif_md_primary_sqlite_create_package_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	ZifMdPrimaryData *fldata = (ZifMdPrimaryData *) data;
	ZifPackageRemote *package;

	package = zif_package_remote_new ();
	zif_package_remote_set_from_repo (package, argc, col_name, argv, fldata->id, NULL);
	g_ptr_array_add (fldata->packages, package);

	return 0;
}

/**
 * zif_md_primary_search:
 **/
static GPtrArray *
zif_md_primary_search (ZifMdPrimary *md, const gchar *pred,
			    GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;
	gboolean ret;
	GError *error_local = NULL;
	ZifMdPrimaryData *data = NULL;
	GPtrArray *array = NULL;

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_primary file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* create data struct we can pass to the callback */
	data = g_new0 (ZifMdPrimaryData, 1);
	data->id = zif_md_get_id (ZIF_MD (md));
	data->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	statement = g_strdup_printf ("SELECT pkgId, name, arch, version, "
				     "epoch, release, summary, description, url, "
				     "rpm_license, rpm_group, size_package, location_href FROM packages %s", pred);
	rc = sqlite3_exec (md->priv->db, statement, zif_md_primary_sqlite_create_package_cb, data, &error_msg);
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
	g_free (statement);
	return array;
}

/**
 * zif_md_primary_resolve:
 * @md: the #ZifMdPrimary object
 * @search: the search term, e.g. "gnome-power-manager"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all remote packages that match the name exactly.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_primary_resolve (ZifMdPrimary *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE name = '%s'", search);
	array = zif_md_primary_search (md, pred, cancellable, completion, error);
	g_free (pred);

	return array;
}

/**
 * zif_md_primary_search_name:
 * @md: the #ZifMdPrimary object
 * @search: the search term, e.g. "power"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the name.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_primary_search_name (ZifMdPrimary *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE name LIKE '%%%s%%'", search);
	array = zif_md_primary_search (md, pred, cancellable, completion, error);
	g_free (pred);

	return array;
}

/**
 * zif_md_primary_search_details:
 * @md: the #ZifMdPrimary object
 * @search: the search term, e.g. "advanced"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the name or description.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_primary_search_details (ZifMdPrimary *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE name LIKE '%%%s%%' OR summary LIKE '%%%s%%' OR description LIKE '%%%s%%'", search, search, search);
	array = zif_md_primary_search (md, pred, cancellable, completion, error);
	g_free (pred);

	return array;
}

/**
 * zif_md_primary_search_group:
 * @md: the #ZifMdPrimary object
 * @search: the search term, e.g. "games/console"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the group.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_primary_search_group (ZifMdPrimary *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE rpm_group = '%s'", search);
	array = zif_md_primary_search (md, pred, cancellable, completion, error);
	g_free (pred);

	return array;
}

/**
 * zif_md_primary_search_pkgid:
 * @md: the #ZifMdPrimary object
 * @search: the search term as a 64 bit hash
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the given pkgId.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_primary_search_pkgid (ZifMdPrimary *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE pkgid = '%s'", search);
	array = zif_md_primary_search (md, pred, cancellable, completion, error);
	g_free (pred);

	return array;
}

/**
 * zif_md_primary_search_pkgkey:
 * @md: the #ZifMdPrimary object
 * @pkgkey: the package key, unique to this sqlite file
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the given pkgId.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.0.1
 **/
static GPtrArray *
zif_md_primary_search_pkgkey (ZifMdPrimary *md, guint pkgkey,
			      GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *pred;
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	pred = g_strdup_printf ("WHERE pkgKey = '%i'", pkgkey);
	array = zif_md_primary_search (md, pred, cancellable, completion, error);
	g_free (pred);

	return array;
}

/**
 * zif_md_primary_sqlite_pkgkey_cb:
 **/
static gint
zif_md_primary_sqlite_pkgkey_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint i;
	guint pkgkey;
	gboolean ret;
	GPtrArray *array = (GPtrArray *) data;

	/* get the ID */
	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "pkgKey") == 0) {
			ret = egg_strtouint (argv[i], &pkgkey);
			if (ret)
				g_ptr_array_add (array, GUINT_TO_POINTER (pkgkey));
			else
				egg_warning ("could not parse pkgKey '%s'", argv[i]);
		} else {
			egg_warning ("unrecognized: %s=%s", col_name[i], argv[i]);
		}
	}
	return 0;
}

/**
 * zif_md_primary_what_provides:
 * @md: the #ZifMdPrimary object
 * @search: the provide, e.g. "mimehandler(application/ogg)"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the given provide.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_primary_what_provides (ZifMdPrimary *md, const gchar *search,
			      GCancellable *cancellable, ZifCompletion *completion, GError **error)
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
	ZifCompletion *completion_local;
	ZifCompletion *completion_loop;
	ZifPackage *package;

	/* setup completion */
	if (md->priv->loaded)
		zif_completion_set_number_steps (completion, 2);
	else
		zif_completion_set_number_steps (completion, 3);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_md_load (ZIF_MD (md), cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_primary file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* create data struct we can pass to the callback */
	pkgkey_array = g_ptr_array_new ();
	statement = g_strdup_printf ("SELECT pkgKey FROM provides WHERE name = '%s'", search);
	rc = sqlite3_exec (md->priv->db, statement, zif_md_primary_sqlite_pkgkey_cb, pkgkey_array, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* output array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* resolve each pkgkey to a package */
	completion_local = zif_completion_get_child (completion);
	if (pkgkey_array->len > 0)
		zif_completion_set_number_steps (completion_local, pkgkey_array->len);
	for (i=0; i<pkgkey_array->len; i++) {
		pkgkey = GPOINTER_TO_UINT (g_ptr_array_index (pkgkey_array, i));

		/* get packages for pkgKey */
		completion_loop = zif_completion_get_child (completion_local);
		array_tmp = zif_md_primary_search_pkgkey (md, pkgkey, cancellable, completion, error);
		if (array_tmp == NULL) {
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		/* check we only got one result */
		if (array_tmp->len == 0) {
			egg_warning ("no package for pkgKey %i", pkgkey);
		} else if (array_tmp->len > 1 || array_tmp->len == 0) {
			egg_warning ("more than one package for pkgKey %i", pkgkey);
		} else {
			package = g_ptr_array_index (array_tmp, 0);
			g_ptr_array_add (array, g_object_ref (package));
		}

		/* clear array */
		g_ptr_array_unref (array_tmp);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	g_free (statement);
	if (pkgkey_array != NULL)
		g_ptr_array_unref (pkgkey_array);
	return array;
}

/**
 * zif_md_primary_find_package:
 * @md: the #ZifMdPrimary object
 * @package_id: the PackageId to match
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match PackageId.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_primary_find_package (ZifMdPrimary *md, const gchar *package_id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *pred;
	GPtrArray *array;
	gchar **split;

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate, TODO: search version (epoch+release) */
	split = pk_package_id_split (package_id);
	pred = g_strdup_printf ("WHERE name = '%s' AND arch = '%s'", split[PK_PACKAGE_ID_NAME], split[PK_PACKAGE_ID_ARCH]);
	array = zif_md_primary_search (md, pred, cancellable, completion, error);
	g_free (pred);
	g_strfreev (split);

	return array;
}

/**
 * zif_md_primary_get_packages:
 * @md: the #ZifMdPrimary object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Returns all packages in the repo.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_primary_get_packages (ZifMdPrimary *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	array = zif_md_primary_search (md, "", cancellable, completion, error);
	return array;
}

/**
 * zif_md_primary_finalize:
 **/
static void
zif_md_primary_finalize (GObject *object)
{
	ZifMdPrimary *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_PRIMARY (object));
	md = ZIF_MD_PRIMARY (object);

	sqlite3_close (md->priv->db);

	G_OBJECT_CLASS (zif_md_primary_parent_class)->finalize (object);
}

/**
 * zif_md_primary_class_init:
 **/
static void
zif_md_primary_class_init (ZifMdPrimaryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_primary_finalize;

	/* map */
	md_class->load = zif_md_primary_load;
	md_class->unload = zif_md_primary_unload;
	g_type_class_add_private (klass, sizeof (ZifMdPrimaryPrivate));
}

/**
 * zif_md_primary_init:
 **/
static void
zif_md_primary_init (ZifMdPrimary *md)
{
	md->priv = ZIF_MD_PRIMARY_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->db = NULL;
}

/**
 * zif_md_primary_new:
 *
 * Return value: A new #ZifMdPrimary class instance.
 *
 * Since: 0.0.1
 **/
ZifMdPrimary *
zif_md_primary_new (void)
{
	ZifMdPrimary *md;
	md = g_object_new (ZIF_TYPE_MD_PRIMARY, NULL);
	return ZIF_MD_PRIMARY (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_md_primary_test (EggTest *test)
{
	ZifMdPrimary *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifPackage *package;
	ZifString *summary;
	GCancellable *cancellable;
	ZifCompletion *completion;

	if (!egg_test_start (test, "ZifMdPrimary"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get md_primary md");
	md = zif_md_primary_new ();
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
	ret = zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_PRIMARY_DB);
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
	ret = zif_md_set_checksum (ZIF_MD (md), "35d817e2bac701525fa72cec57387a2e3457bf32642adeee1e345cc180044c86");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum uncompressed");
	ret = zif_md_set_checksum_uncompressed (ZIF_MD (md), "9b2b072a83b5175bc88d03ee64b52b39c0d40fec1516baa62dba81eea73cc645");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_md_set_filename (ZIF_MD (md), "../test/cache/fedora/35d817e2bac701525fa72cec57387a2e3457bf32642adeee1e345cc180044c86-primary.sqlite.bz2");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "search for files");
	array = zif_md_primary_resolve (md, "gnome-power-manager", cancellable, completion, &error);
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
	g_ptr_array_unref (array);

	g_object_unref (cancellable);
	g_object_unref (completion);
	g_object_unref (md);

	egg_test_end (test);
}
#endif

