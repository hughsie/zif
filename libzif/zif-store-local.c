/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-store-local
 * @short_description: A local store is a store that can operate on installed packages
 *
 * A #ZifStoreLocal is a subclassed #ZifStore and operates on installed objects.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>

#include <glib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <fcntl.h>
#include <packagekit-glib/packagekit.h>

#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-groups.h"
#include "zif-package-local.h"
#include "zif-monitor.h"
#include "zif-string.h"
#include "zif-depend-array.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_STORE_LOCAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_LOCAL, ZifStoreLocalPrivate))

struct ZifStoreLocalPrivate
{
	gboolean		 loaded;
	gchar			*prefix;
	GPtrArray		*packages;
	ZifGroups		*groups;
	ZifMonitor		*monitor;
};


G_DEFINE_TYPE (ZifStoreLocal, zif_store_local, ZIF_TYPE_STORE)
static gpointer zif_store_local_object = NULL;

/**
 * zif_store_local_set_prefix:
 * @store: the #ZifStoreLocal object
 * @prefix: the install root, e.g. "/"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets the prefix to use for the install root.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_store_local_set_prefix (ZifStoreLocal *store, const gchar *prefix, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *filename = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), FALSE);
	g_return_val_if_fail (store->priv->prefix == NULL, FALSE);
	g_return_val_if_fail (!store->priv->loaded, FALSE);
	g_return_val_if_fail (prefix != NULL, FALSE);

	/* check file exists */
	ret = g_file_test (prefix, G_FILE_TEST_IS_DIR);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "prefix %s does not exist", prefix);
		goto out;
	}

	/* setup watch */
	filename = g_build_filename (prefix, "var", "lib", "rpm", "Packages", NULL);
	ret = zif_monitor_add_watch (store->priv->monitor, filename, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	store->priv->prefix = g_strdup (prefix);
out:
	g_free (filename);
	return ret;
}

/**
 * zif_store_local_load:
 **/
static gboolean
zif_store_local_load (ZifStore *store, GError **error)
{
	gint retval;
	gboolean ret = TRUE;
	rpmdbMatchIterator mi;
	Header header;
	ZifPackageLocal *package;
	rpmdb db;
	GError *error_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), FALSE);
	g_return_val_if_fail (local->priv->prefix != NULL, FALSE);
	g_return_val_if_fail (local->priv->packages != NULL, FALSE);

	/* already loaded */
	if (local->priv->loaded)
		goto out;

	retval = rpmdbOpen (local->priv->prefix, &db, O_RDONLY, 0777);
	if (retval != 0) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to open rpmdb");
		ret = FALSE;
		goto out;
	}

	/* get list */
	mi = rpmdbInitIterator (db, RPMDBI_PACKAGES, NULL, 0);
	if (mi == NULL)
		egg_warning ("failed to get iterator");
	do {
		header = rpmdbNextIterator (mi);
		if (header == NULL)
			break;
		package = zif_package_local_new ();
		ret = zif_package_local_set_from_header (package, header, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to set from header: %s", error_local->message);
			g_error_free (error_local);	
			g_object_unref (package);		
			break;
		}
		g_ptr_array_add (local->priv->packages, package);
	} while (TRUE);
	rpmdbFreeIterator (mi);
	rpmdbClose (db);

	/* okay */
	local->priv->loaded = TRUE;
out:
	return ret;
}

/**
 * zif_store_local_search_name:
 **/
static GPtrArray *
zif_store_local_search_name (ZifStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const PkPackageId *id;
	GError *error_local = NULL;
	gboolean ret;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = zif_store_local_load (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = g_ptr_array_new ();
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		id = zif_package_get_id (package);
		if (strcasestr (id->name, search) != NULL)
			g_ptr_array_add (array, g_object_ref (package));
	}
out:
	return array;
}

/**
 * zif_store_local_search_category:
 **/
static GPtrArray *
zif_store_local_search_category (ZifStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifString *category;
	GError *error_local = NULL;
	gboolean ret;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = zif_store_local_load (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = g_ptr_array_new ();
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		category = zif_package_get_category (package, NULL);
		if (strcmp (zif_string_get_value (category), search) == 0)
			g_ptr_array_add (array, g_object_ref (package));
		zif_string_unref (category);
	}
out:
	return array;
}

/**
 * zif_store_local_earch_details:
 **/
static GPtrArray *
zif_store_local_search_details (ZifStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const PkPackageId *id;
	ZifString *description;
	GError *error_local = NULL;
	gboolean ret;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = zif_store_local_load (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = g_ptr_array_new ();
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		id = zif_package_get_id (package);
		description = zif_package_get_description (package, NULL);
		if (strcasestr (id->name, search) != NULL)
			g_ptr_array_add (array, g_object_ref (package));
		else if (strcasestr (zif_string_get_value (description), search) != NULL)
			g_ptr_array_add (array, g_object_ref (package));
		zif_string_unref (description);
	}
out:
	return array;
}

/**
 * zif_store_local_search_group:
 **/
static GPtrArray *
zif_store_local_search_group (ZifStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package;
	PkGroupEnum group_tmp;
	GError *error_local = NULL;
	gboolean ret;
	PkGroupEnum group;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = zif_store_local_load (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	group = pk_group_enum_from_text (search);
	array = g_ptr_array_new ();
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		group_tmp = zif_package_get_group (package, NULL);
		if (group == group_tmp)
			g_ptr_array_add (array, g_object_ref (package));
	}
out:
	return array;
}

/**
 * zif_store_local_search_file:
 **/
static GPtrArray *
zif_store_local_search_file (ZifStore *store, const gchar *search, GError **error)
{
	guint i, j;
	guint len;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifStringArray *files;
	GError *error_local = NULL;
	const gchar *filename;
	gboolean ret;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = zif_store_local_load (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = g_ptr_array_new ();
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		files = zif_package_get_files (package, &error_local);
		if (files == NULL) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to get file lists: %s", error_local->message);
			g_error_free (error_local);
			g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
			g_ptr_array_free (array, TRUE);
			array = NULL;
			break;
		}
		len = zif_string_array_get_length (files);
		for (j=0; j<len; j++) {
			filename = zif_string_array_get_value (files, j);
			if (g_strcmp0 (search, filename) == 0)
				g_ptr_array_add (array, g_object_ref (package));
		}
		zif_string_array_unref (files);
	}
out:
	return array;
}

/**
 * zif_store_local_resolve:
 **/
static GPtrArray *
zif_store_local_resolve (ZifStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const PkPackageId *id;
	GError *error_local = NULL;
	gboolean ret;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = zif_store_local_load (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = g_ptr_array_new ();
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		id = zif_package_get_id (package);
		if (strcmp (id->name, search) == 0)
			g_ptr_array_add (array, g_object_ref (package));
	}
out:
	return array;
}

/**
 * zif_store_local_what_provides:
 **/
static GPtrArray *
zif_store_local_what_provides (ZifStore *store, const gchar *search, GError **error)
{
	guint i;
	guint j;
	guint len;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifDependArray *provides;
	GError *error_local = NULL;
	gboolean ret;
	const ZifDepend *provide;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = zif_store_local_load (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = g_ptr_array_new ();
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		provides = zif_package_get_provides (package, NULL);
		len = zif_depend_array_get_length (provides);
		for (j=0; j<len; j++) {
			provide = zif_depend_array_get_value (provides, j);
			if (strcmp (provide->name, search) == 0) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}
	}
out:
	return array;
}

/**
 * zif_store_local_get_packages:
 **/
static GPtrArray *
zif_store_local_get_packages (ZifStore *store, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package;
	GError *error_local = NULL;
	gboolean ret;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = zif_store_local_load (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = g_ptr_array_new ();
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		g_ptr_array_add (array, g_object_ref (package));
	}
out:
	return array;
}

/**
 * zif_store_local_get_id:
 **/
static const gchar *
zif_store_local_get_id (ZifStore *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	return "installed";
}

/**
 * zif_store_local_print:
 **/
static void
zif_store_local_print (ZifStore *store)
{
	guint i;
	ZifPackage *package;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_if_fail (ZIF_IS_STORE_LOCAL (store));
	g_return_if_fail (local->priv->prefix != NULL);
	g_return_if_fail (local->priv->packages->len != 0);

	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		zif_package_print (package);
	}
}

/**
 * zif_store_local_file_monitor_cb:
 **/
static void
zif_store_local_file_monitor_cb (ZifMonitor *monitor, ZifStoreLocal *store)
{
	store->priv->loaded = FALSE;

	g_ptr_array_foreach (store->priv->packages, (GFunc) g_object_unref, NULL);
	g_ptr_array_set_size (store->priv->packages, 0);

	egg_debug ("rpmdb changed");
}

/**
 * zif_store_local_finalize:
 **/
static void
zif_store_local_finalize (GObject *object)
{
	ZifStoreLocal *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_LOCAL (object));
	store = ZIF_STORE_LOCAL (object);

	g_ptr_array_foreach (store->priv->packages, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (store->priv->packages, TRUE);
	g_object_unref (store->priv->groups);
	g_object_unref (store->priv->monitor);
	g_free (store->priv->prefix);

	G_OBJECT_CLASS (zif_store_local_parent_class)->finalize (object);
}

/**
 * zif_store_local_class_init:
 **/
static void
zif_store_local_class_init (ZifStoreLocalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifStoreClass *store_class = ZIF_STORE_CLASS (klass);
	object_class->finalize = zif_store_local_finalize;

	/* map */
	store_class->load = zif_store_local_load;
	store_class->search_name = zif_store_local_search_name;
	store_class->search_category = zif_store_local_search_category;
	store_class->search_details = zif_store_local_search_details;
	store_class->search_group = zif_store_local_search_group;
	store_class->search_file = zif_store_local_search_file;
	store_class->resolve = zif_store_local_resolve;
	store_class->what_provides = zif_store_local_what_provides;
	store_class->get_packages = zif_store_local_get_packages;
//	store_class->find_package = zif_store_local_find_package;
	store_class->get_id = zif_store_local_get_id;
	store_class->print = zif_store_local_print;

	g_type_class_add_private (klass, sizeof (ZifStoreLocalPrivate));
}

/**
 * zif_store_local_init:
 **/
static void
zif_store_local_init (ZifStoreLocal *store)
{
	store->priv = ZIF_STORE_LOCAL_GET_PRIVATE (store);
	store->priv->packages = g_ptr_array_new ();
	store->priv->groups = zif_groups_new ();
	store->priv->monitor = zif_monitor_new ();
	store->priv->prefix = NULL;
	store->priv->loaded = FALSE;
	g_signal_connect (store->priv->monitor, "changed", G_CALLBACK (zif_store_local_file_monitor_cb), store);
}

/**
 * zif_store_local_new:
 *
 * Return value: A new #ZifStoreLocal class instance.
 **/
ZifStoreLocal *
zif_store_local_new (void)
{
	if (zif_store_local_object != NULL) {
		g_object_ref (zif_store_local_object);
	} else {
		zif_store_local_object = g_object_new (ZIF_TYPE_STORE_LOCAL, NULL);
		g_object_add_weak_pointer (zif_store_local_object, &zif_store_local_object);
	}
	return ZIF_STORE_LOCAL (zif_store_local_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_store_local_test (EggTest *test)
{
	ZifStoreLocal *store;
	gboolean ret;
	GPtrArray *array;
	ZifPackage *package;
	ZifGroups *groups;
	GError *error = NULL;
	guint elapsed;
	const gchar *text;
	ZifString *string;
	const PkPackageId *id;

	if (!egg_test_start (test, "ZifStoreLocal"))
		return;

	/************************************************************/
	egg_test_title (test, "get groups");
	groups = zif_groups_new ();
	ret = zif_groups_set_mapping_file (groups, "../test/share/yum-comps-groups.conf", NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get store");
	store = zif_store_local_new ();
	egg_test_assert (test, store != NULL);

	/************************************************************/
	egg_test_title (test, "set prefix");
	ret = zif_store_local_set_prefix (store, "/", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set prefix '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_store_local_load (ZIF_STORE (store), &error);
	elapsed = egg_test_elapsed (test);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "check time < 10s");
	if (elapsed < 10000)
		egg_test_success (test, "time to load = %ims", elapsed);
	else
		egg_test_failed (test, "time to load = %ims", elapsed);

	/************************************************************/
	egg_test_title (test, "load (again)");
	ret = zif_store_local_load (ZIF_STORE (store), &error);
	elapsed = egg_test_elapsed (test);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "check time < 10ms");
	if (elapsed < 10)
		egg_test_success (test, "time to load = %ims", elapsed);
	else
		egg_test_failed (test, "time to load = %ims", elapsed);

	/************************************************************/
	egg_test_title (test, "resolve");
	array = zif_store_local_resolve (ZIF_STORE (store), "kernel", NULL);
	elapsed = egg_test_elapsed (test);
	if (array->len >= 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "check time < 10ms");
	if (elapsed < 10)
		egg_test_success (test, "time to load = %ims", elapsed);
	else
		egg_test_failed (test, "time to load = %ims", elapsed);

	/************************************************************/
	egg_test_title (test, "search name");
	array = zif_store_local_search_name (ZIF_STORE (store), "gnome-p", NULL);
	if (array->len > 10)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "search details");
	array = zif_store_local_search_details (ZIF_STORE (store), "manage packages", NULL);
	if (array->len == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "what-provides");
	array = zif_store_local_what_provides (ZIF_STORE (store), "config(PackageKit)", NULL);
	if (array->len == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	/* get this package */
	package = g_ptr_array_index (array, 0);

	/************************************************************/
	egg_test_title (test, "get id");
	id = zif_package_get_id (package);
	if (egg_strequal (id->name, "PackageKit"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect name: %s", id->name);

	/************************************************************/
	egg_test_title (test, "get package id");
	text = zif_package_get_package_id (package);
	if (g_str_has_suffix (text, ";installed"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect package_id: %s", text);

	/************************************************************/
	egg_test_title (test, "get summary");
	string = zif_package_get_summary (package, NULL);
	if (egg_strequal (zif_string_get_value (string), "Package management service"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect summary: %s", zif_string_get_value (string));
	zif_string_unref (string);

	/************************************************************/
	egg_test_title (test, "get license");
	string = zif_package_get_license (package, NULL);
	if (egg_strequal (zif_string_get_value (string), "GPLv2+"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect license: %s", zif_string_get_value (string));
	zif_string_unref (string);

	/************************************************************/
	egg_test_title (test, "get category");
	string = zif_package_get_category (package, NULL);
	if (egg_strequal (zif_string_get_value (string), "System Environment/Libraries"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect category: %s", zif_string_get_value (string));
	zif_string_unref (string);

	/************************************************************/
	egg_test_title (test, "is devel");
	ret = zif_package_is_devel (package);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "is gui");
	ret = zif_package_is_gui (package);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "is installed");
	ret = zif_package_is_installed (package);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "is free");
	ret = zif_package_is_free (package);
	egg_test_assert (test, ret);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (store);
	g_object_unref (groups);

	egg_test_end (test);
}
#endif
