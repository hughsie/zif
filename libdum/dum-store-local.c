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

#include "dum-store.h"
#include "dum-store-local.h"
#include "dum-groups.h"
#include "dum-package-local.h"
#include "dum-monitor.h"
#include "dum-string.h"
#include "dum-depend-array.h"

#include "egg-debug.h"
#include "egg-string.h"

#define DUM_STORE_LOCAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_STORE_LOCAL, DumStoreLocalPrivate))

struct DumStoreLocalPrivate
{
	gboolean		 loaded;
	gchar			*prefix;
	GPtrArray		*packages;
	DumGroups		*groups;
	DumMonitor		*monitor;
};


G_DEFINE_TYPE (DumStoreLocal, dum_store_local, DUM_TYPE_STORE)
static gpointer dum_store_local_object = NULL;

/**
 * dum_store_local_set_prefix:
 **/
gboolean
dum_store_local_set_prefix (DumStoreLocal *store, const gchar *prefix, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *filename = NULL;

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), FALSE);
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
	ret = dum_monitor_add_watch (store->priv->monitor, filename, &error_local);
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
 * dum_store_local_load:
 **/
static gboolean
dum_store_local_load (DumStore *store, GError **error)
{
	gint retval;
	gboolean ret = TRUE;
	rpmdbMatchIterator mi;
	Header header;
	DumPackageLocal *package;
	rpmdb db;
	GError *error_local = NULL;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), FALSE);
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
		package = dum_package_local_new ();
		ret = dum_package_local_set_from_header (package, header, &error_local);
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
 * dum_store_local_search_name:
 **/
static GPtrArray *
dum_store_local_search_name (DumStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	DumPackage *package;
	const PkPackageId *id;
	GError *error_local = NULL;
	gboolean ret;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = dum_store_local_load (store, &error_local);
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
		id = dum_package_get_id (package);
		if (strcasestr (id->name, search) != NULL)
			g_ptr_array_add (array, g_object_ref (package));
	}
out:
	return array;
}

/**
 * dum_store_local_search_category:
 **/
static GPtrArray *
dum_store_local_search_category (DumStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	DumPackage *package;
	DumString *category;
	GError *error_local = NULL;
	gboolean ret;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = dum_store_local_load (store, &error_local);
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
		category = dum_package_get_category (package, NULL);
		if (strcmp (category->value, search) == 0)
			g_ptr_array_add (array, g_object_ref (package));
		dum_string_unref (category);
	}
out:
	return array;
}

/**
 * dum_store_local_earch_details:
 **/
static GPtrArray *
dum_store_local_search_details (DumStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	DumPackage *package;
	const PkPackageId *id;
	DumString *description;
	GError *error_local = NULL;
	gboolean ret;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = dum_store_local_load (store, &error_local);
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
		id = dum_package_get_id (package);
		description = dum_package_get_description (package, NULL);
		if (strcasestr (id->name, search) != NULL)
			g_ptr_array_add (array, g_object_ref (package));
		else if (strcasestr (description->value, search) != NULL)
			g_ptr_array_add (array, g_object_ref (package));
		dum_string_unref (description);
	}
out:
	return array;
}

/**
 * dum_store_local_search_group:
 **/
static GPtrArray *
dum_store_local_search_group (DumStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	DumPackage *package;
	PkGroupEnum group_tmp;
	GError *error_local = NULL;
	gboolean ret;
	PkGroupEnum group;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = dum_store_local_load (store, &error_local);
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
		group_tmp = dum_package_get_group (package, NULL);
		if (group == group_tmp)
			g_ptr_array_add (array, g_object_ref (package));
	}
out:
	return array;
}

/**
 * dum_store_local_search_file:
 **/
static GPtrArray *
dum_store_local_search_file (DumStore *store, const gchar *search, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	DumPackage *package;
	DumStringArray *files;
	GError *error_local = NULL;
	const gchar *filename;
	gboolean ret;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = dum_store_local_load (store, &error_local);
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
		files = dum_package_get_files (package, &error_local);
		if (files == NULL) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to get file lists: %s", error_local->message);
			g_error_free (error_local);
			g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
			g_ptr_array_free (array, TRUE);
			array = NULL;
			break;
		}
		for (j=0; j<files->value->len; j++) {
			filename = g_ptr_array_index (files->value, j);
			if (g_strcmp0 (search, filename) == 0)
				g_ptr_array_add (array, g_object_ref (package));
		}
		dum_string_array_unref (files);
	}
out:
	return array;
}

/**
 * dum_store_local_resolve:
 **/
static GPtrArray *
dum_store_local_resolve (DumStore *store, const gchar *search, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	DumPackage *package;
	const PkPackageId *id;
	GError *error_local = NULL;
	gboolean ret;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = dum_store_local_load (store, &error_local);
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
		id = dum_package_get_id (package);
		if (strcmp (id->name, search) == 0)
			g_ptr_array_add (array, g_object_ref (package));
	}
out:
	return array;
}

/**
 * dum_store_local_what_provides:
 **/
static GPtrArray *
dum_store_local_what_provides (DumStore *store, const gchar *search, GError **error)
{
	guint i;
	guint j;
	GPtrArray *array = NULL;
	DumPackage *package;
	DumDependArray *provides;
	GError *error_local = NULL;
	gboolean ret;
	DumDepend *provide;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = dum_store_local_load (store, &error_local);
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
		provides = dum_package_get_provides (package, NULL);
		for (j=0; j<provides->value->len; j++) {
			provide = g_ptr_array_index (provides->value, j);
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
 * dum_store_local_get_packages:
 **/
static GPtrArray *
dum_store_local_get_packages (DumStore *store, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	DumPackage *package;
	GError *error_local = NULL;
	gboolean ret;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		ret = dum_store_local_load (store, &error_local);
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
 * dum_store_local_get_id:
 **/
static const gchar *
dum_store_local_get_id (DumStore *store)
{
	g_return_val_if_fail (DUM_IS_STORE_LOCAL (store), NULL);
	return "installed";
}

/**
 * dum_store_local_print:
 **/
static void
dum_store_local_print (DumStore *store)
{
	guint i;
	DumPackage *package;
	DumStoreLocal *local = DUM_STORE_LOCAL (store);

	g_return_if_fail (DUM_IS_STORE_LOCAL (store));
	g_return_if_fail (local->priv->prefix != NULL);
	g_return_if_fail (local->priv->packages->len != 0);

	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		dum_package_print (package);
	}
}

/**
 * dum_store_local_file_monitor_cb:
 **/
static void
dum_store_local_file_monitor_cb (DumMonitor *monitor, DumStoreLocal *store)
{
	store->priv->loaded = FALSE;

	g_ptr_array_foreach (store->priv->packages, (GFunc) g_object_unref, NULL);
	g_ptr_array_set_size (store->priv->packages, 0);

	egg_debug ("rpmdb changed");
}

/**
 * dum_store_local_finalize:
 **/
static void
dum_store_local_finalize (GObject *object)
{
	DumStoreLocal *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_STORE_LOCAL (object));
	store = DUM_STORE_LOCAL (object);

	g_ptr_array_foreach (store->priv->packages, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (store->priv->packages, TRUE);
	g_object_unref (store->priv->groups);
	g_object_unref (store->priv->monitor);
	g_free (store->priv->prefix);

	G_OBJECT_CLASS (dum_store_local_parent_class)->finalize (object);
}

/**
 * dum_store_local_class_init:
 **/
static void
dum_store_local_class_init (DumStoreLocalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	DumStoreClass *store_class = DUM_STORE_CLASS (klass);
	object_class->finalize = dum_store_local_finalize;

	/* map */
	store_class->load = dum_store_local_load;
	store_class->search_name = dum_store_local_search_name;
	store_class->search_category = dum_store_local_search_category;
	store_class->search_details = dum_store_local_search_details;
	store_class->search_group = dum_store_local_search_group;
	store_class->search_file = dum_store_local_search_file;
	store_class->resolve = dum_store_local_resolve;
	store_class->what_provides = dum_store_local_what_provides;
	store_class->get_packages = dum_store_local_get_packages;
//	store_class->find_package = dum_store_local_find_package;
	store_class->get_id = dum_store_local_get_id;
	store_class->print = dum_store_local_print;

	g_type_class_add_private (klass, sizeof (DumStoreLocalPrivate));
}

/**
 * dum_store_local_init:
 **/
static void
dum_store_local_init (DumStoreLocal *store)
{
	store->priv = DUM_STORE_LOCAL_GET_PRIVATE (store);
	store->priv->packages = g_ptr_array_new ();
	store->priv->groups = dum_groups_new ();
	store->priv->monitor = dum_monitor_new ();
	store->priv->prefix = NULL;
	store->priv->loaded = FALSE;
	g_signal_connect (store->priv->monitor, "changed", G_CALLBACK (dum_store_local_file_monitor_cb), store);
}

/**
 * dum_store_local_new:
 * Return value: A new store_local class instance.
 **/
DumStoreLocal *
dum_store_local_new (void)
{
	if (dum_store_local_object != NULL) {
		g_object_ref (dum_store_local_object);
	} else {
		dum_store_local_object = g_object_new (DUM_TYPE_STORE_LOCAL, NULL);
		g_object_add_weak_pointer (dum_store_local_object, &dum_store_local_object);
	}
	return DUM_STORE_LOCAL (dum_store_local_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_store_local_test (EggTest *test)
{
	DumStoreLocal *store;
	gboolean ret;
	GPtrArray *array;
	DumPackage *package;
	DumGroups *groups;
	GError *error = NULL;
	guint elapsed;
	const gchar *text;
	DumString *string;
	const PkPackageId *id;

	if (!egg_test_start (test, "DumStoreLocal"))
		return;

	/************************************************************/
	egg_test_title (test, "get groups");
	groups = dum_groups_new ();
	ret = dum_groups_set_mapping_file (groups, "../test/share/yum-comps-groups.conf", NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get store");
	store = dum_store_local_new ();
	egg_test_assert (test, store != NULL);

	/************************************************************/
	egg_test_title (test, "set prefix");
	ret = dum_store_local_set_prefix (store, "/", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set prefix '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "load");
	ret = dum_store_local_load (DUM_STORE (store), &error);
	elapsed = egg_test_elapsed (test);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "check time < 1s");
	if (elapsed < 1000)
		egg_test_success (test, "time to load = %ims", elapsed);
	else
		egg_test_failed (test, "time to load = %ims", elapsed);

	/************************************************************/
	egg_test_title (test, "load (again)");
	ret = dum_store_local_load (DUM_STORE (store), &error);
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
	array = dum_store_local_resolve (DUM_STORE (store), "kernel", NULL);
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
	array = dum_store_local_search_name (DUM_STORE (store), "gnome-p", NULL);
	if (array->len > 10)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "search details");
	array = dum_store_local_search_details (DUM_STORE (store), "manage packages", NULL);
	if (array->len == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "what-provides");
	array = dum_store_local_what_provides (DUM_STORE (store), "config(PackageKit)", NULL);
	if (array->len == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	/* get this package */
	package = g_ptr_array_index (array, 0);

	/************************************************************/
	egg_test_title (test, "get id");
	id = dum_package_get_id (package);
	if (egg_strequal (id->name, "PackageKit"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect name: %s", id->name);

	/************************************************************/
	egg_test_title (test, "get package id");
	text = dum_package_get_package_id (package);
	if (g_str_has_suffix (text, ";installed"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect package_id: %s", text);

	/************************************************************/
	egg_test_title (test, "get summary");
	string = dum_package_get_summary (package, NULL);
	if (egg_strequal (string->value, "Package management service"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect summary: %s", string->value);
	dum_string_unref (string);

	/************************************************************/
	egg_test_title (test, "get license");
	string = dum_package_get_license (package, NULL);
	if (egg_strequal (string->value, "GPLv2+"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect license: %s", string->value);
	dum_string_unref (string);

	/************************************************************/
	egg_test_title (test, "get category");
	string = dum_package_get_category (package, NULL);
	if (egg_strequal (string->value, "System Environment/Libraries"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect category: %s", string->value);
	dum_string_unref (string);

	/************************************************************/
	egg_test_title (test, "is devel");
	ret = dum_package_is_devel (package);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "is gui");
	ret = dum_package_is_gui (package);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "is installed");
	ret = dum_package_is_installed (package);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "is free");
	ret = dum_package_is_free (package);
	egg_test_assert (test, ret);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (store);
	g_object_unref (groups);

	egg_test_end (test);
}
#endif

