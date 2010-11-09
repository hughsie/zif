/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-store-meta
 * @short_description: A meta store is a store that can operate on
 * installed, remote or installed packages.
 *
 * The primary purpose of #ZifStoreMeta is to be a general basket to
 * put #ZifPackages in, without actually getting the packages from any
 * remote or local source. It can be thought of as an in-memory sack.
 *
 * A #ZifStoreMeta is a subclassed #ZifStore and operates on packages.
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

#include "zif-store.h"
#include "zif-store-meta.h"
#include "zif-groups.h"
#include "zif-package-meta.h"
#include "zif-monitor.h"
#include "zif-string.h"
#include "zif-depend.h"
#include "zif-lock.h"
#include "zif-utils.h"

#define ZIF_STORE_META_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_META, ZifStoreMetaPrivate))

struct _ZifStoreMetaPrivate
{
	GPtrArray		*packages;
};

G_DEFINE_TYPE (ZifStoreMeta, zif_store_meta, ZIF_TYPE_STORE)

/**
 * zif_store_meta_locate_package:
 **/
static ZifPackage *
zif_store_meta_locate_package (ZifStoreMeta *store, ZifPackage *package)
{
	guint i;
	const gchar *package_id;
	ZifPackage *package_tmp;

	package_id = zif_package_get_id (package);
	for (i=0; i<store->priv->packages->len; i++) {
		package_tmp = g_ptr_array_index (store->priv->packages, i);
		if (g_strcmp0 (package_id, zif_package_get_id (package_tmp)) == 0)
			return package_tmp;
	}
	return NULL;
}

/**
 * zif_store_meta_add_package:
 * @store: the #ZifStoreMeta object
 * @package: a #ZifPackage object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Adds a package to the virtual store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.3
 **/
gboolean
zif_store_meta_add_package (ZifStoreMeta *store, ZifPackage *package, GError **error)
{
	gboolean ret = TRUE;
	ZifPackage *package_tmp;

	g_return_val_if_fail (ZIF_IS_STORE_META (store), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check it's not already added */
	package_tmp = zif_store_meta_locate_package (store, package);
	if (package_tmp != NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "already added %s",
			     zif_package_get_id (package));
		goto out;
	}

	/* just add */
	g_ptr_array_add (store->priv->packages, g_object_ref (package));
out:
	return ret;
}

/**
 * zif_store_meta_add_packages:
 * @store: the #ZifStoreMeta object
 * @array: an array of #ZifPackage's
 * @error: a #GError which is used on failure, or %NULL
 *
 * Adds an array of packages to the virtual store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.3
 **/
gboolean
zif_store_meta_add_packages (ZifStoreMeta *store, GPtrArray *array, GError **error)
{
	gboolean ret = TRUE;
	ZifPackage *package;
	guint i;

	g_return_val_if_fail (ZIF_IS_STORE_META (store), FALSE);
	g_return_val_if_fail (array != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* just add */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_store_meta_add_package (store, package, error);
		if (!ret)
			break;
	}
	return ret;
}

/**
 * zif_store_meta_remove_package:
 * @store: the #ZifStoreMeta object
 * @package: a #ZifPackage object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Removes a package from the virtual store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.3
 **/
gboolean
zif_store_meta_remove_package (ZifStoreMeta *store, ZifPackage *package, GError **error)
{
	gboolean ret = TRUE;
	ZifPackage *package_tmp;

	g_return_val_if_fail (ZIF_IS_STORE_META (store), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check it's not already removeed */
	package_tmp = zif_store_meta_locate_package (store, package);
	if (package_tmp == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "package not found in array %s",
			     zif_package_get_id (package));
		goto out;
	}

	/* just remove */
	g_ptr_array_remove (store->priv->packages, package_tmp);
out:
	return ret;
}

/**
 * zif_store_meta_remove_packages:
 * @store: the #ZifStoreMeta object
 * @array: an array of #ZifPackage's
 * @error: a #GError which is used on failure, or %NULL
 *
 * Removes an array of packages from the virtual store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.3
 **/
gboolean
zif_store_meta_remove_packages (ZifStoreMeta *store, GPtrArray *array, GError **error)
{
	gboolean ret = TRUE;
	ZifPackage *package;
	guint i;

	g_return_val_if_fail (ZIF_IS_STORE_META (store), FALSE);
	g_return_val_if_fail (array != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* just remove */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_store_meta_remove_package (store, package, error);
		if (!ret)
			break;
	}
	return ret;
}

/**
 * zif_store_meta_search_name:
 **/
static GPtrArray *
zif_store_meta_search_name (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const gchar *package_id;
	gchar *split_name;
	gboolean ret = FALSE;
	ZifStoreMeta *meta = ZIF_STORE_META (store);

	g_return_val_if_fail (ZIF_IS_STORE_META (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* we just search a virtual in-memory-sack */
	zif_state_set_number_steps (state, meta->priv->packages->len);

	/* check we have packages */
	if (meta->priv->packages->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "cannot search name, no packages in meta sack");
		goto out;
	}

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<meta->priv->packages->len;i++) {
		package = g_ptr_array_index (meta->priv->packages, i);
		package_id = zif_package_get_id (package);
		split_name = zif_package_id_get_name (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcasestr (split_name, search[j]) != NULL) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}
		g_free (split_name);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}
out:
	return array;
}

/**
 * zif_store_meta_resolve:
 **/
static GPtrArray *
zif_store_meta_resolve (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const gchar *package_id;
	gboolean ret = FALSE;
	gchar *split_name;
	ZifStoreMeta *meta = ZIF_STORE_META (store);

	g_return_val_if_fail (ZIF_IS_STORE_META (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* check we have packages */
	if (meta->priv->packages->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "cannot resolve, no packages in meta sack");
		goto out;
	}

	/* we just search a virtual in-memory-sack */
	zif_state_set_number_steps (state, meta->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<meta->priv->packages->len;i++) {
		package = g_ptr_array_index (meta->priv->packages, i);
		package_id = zif_package_get_id (package);
		split_name = zif_package_id_get_name (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcmp (split_name, search[j]) == 0) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}
		g_free (split_name);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}
out:
	return array;
}

/**
 * zif_store_meta_get_packages:
 **/
static GPtrArray *
zif_store_meta_get_packages (ZifStore *store, ZifState *state, GError **error)
{
	ZifStoreMeta *meta = ZIF_STORE_META (store);

	g_return_val_if_fail (ZIF_IS_STORE_META (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	return g_ptr_array_ref (meta->priv->packages);
}

/**
 * zif_store_meta_find_package:
 **/
static ZifPackage *
zif_store_meta_find_package (ZifStore *store, const gchar *package_id, ZifState *state, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package = NULL;
	ZifPackage *package_tmp = NULL;
	gboolean ret = FALSE;
	const gchar *package_id_tmp;
	ZifStoreMeta *meta = ZIF_STORE_META (store);

	g_return_val_if_fail (ZIF_IS_STORE_META (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* check we have packages */
	if (meta->priv->packages->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "cannot find package, no packages in meta sack");
		goto out;
	}

	/* we just search a virtual in-memory-sack */
	zif_state_set_number_steps (state, meta->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<meta->priv->packages->len;i++) {
		package_tmp = g_ptr_array_index (meta->priv->packages, i);
		package_id_tmp = zif_package_get_id (package_tmp);
		if (g_strcmp0 (package_id_tmp, package_id) == 0)
			g_ptr_array_add (array, g_object_ref (package_tmp));

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* nothing */
	if (array->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_TO_FIND,
				     "failed to find package");
		goto out;
	}

	/* more than one match */
	if (array->len > 1) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_MULTIPLE_MATCHES,
				     "more than one match");
		goto out;
	}

	/* return ref to package */
	package = g_object_ref (g_ptr_array_index (array, 0));
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return package;
}

/**
 * zif_store_meta_get_id:
 **/
static const gchar *
zif_store_meta_get_id (ZifStore *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_META (store), NULL);
	return "meta";
}

/**
 * zif_store_meta_print:
 **/
static void
zif_store_meta_print (ZifStore *store)
{
	guint i;
	ZifPackage *package;
	ZifStoreMeta *meta = ZIF_STORE_META (store);

	g_return_if_fail (ZIF_IS_STORE_META (store));
	g_return_if_fail (meta->priv->packages->len != 0);

	for (i=0;i<meta->priv->packages->len;i++) {
		package = g_ptr_array_index (meta->priv->packages, i);
		zif_package_print (package);
	}
}

/**
 * zif_store_meta_finalize:
 **/
static void
zif_store_meta_finalize (GObject *object)
{
	ZifStoreMeta *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_META (object));
	store = ZIF_STORE_META (object);

	g_ptr_array_unref (store->priv->packages);

	G_OBJECT_CLASS (zif_store_meta_parent_class)->finalize (object);
}

/**
 * zif_store_meta_class_init:
 **/
static void
zif_store_meta_class_init (ZifStoreMetaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifStoreClass *store_class = ZIF_STORE_CLASS (klass);
	object_class->finalize = zif_store_meta_finalize;

	/* map */
	store_class->search_name = zif_store_meta_search_name;
	store_class->resolve = zif_store_meta_resolve;
	store_class->get_packages = zif_store_meta_get_packages;
	store_class->find_package = zif_store_meta_find_package;
	store_class->get_id = zif_store_meta_get_id;
	store_class->print = zif_store_meta_print;

	g_type_class_add_private (klass, sizeof (ZifStoreMetaPrivate));
}

/**
 * zif_store_meta_init:
 **/
static void
zif_store_meta_init (ZifStoreMeta *store)
{
	store->priv = ZIF_STORE_META_GET_PRIVATE (store);
	store->priv->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * zif_store_meta_new:
 *
 * Return value: A new #ZifStoreMeta class instance.
 *
 * Since: 0.1.3
 **/
ZifStore *
zif_store_meta_new (void)
{
	ZifStoreMeta *store_meta;
	store_meta = g_object_new (ZIF_TYPE_STORE_META, NULL);
	return ZIF_STORE (store_meta);
}

