/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2011 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-store
 * @short_description: Collection of packages
 *
 * #ZifStoreLocal, #ZifStoreRemote and #ZifStoreMeta all implement #ZifStore.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>

#include <glib.h>
#include <rpm/rpmlib.h>

#include "zif-array.h"
#include "zif-object-array.h"
#include "zif-package-array.h"
#include "zif-package.h"
#include "zif-store.h"
#include "zif-utils.h"

#define ZIF_STORE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE, ZifStorePrivate))

struct _ZifStorePrivate
{
	ZifArray		*packages;
	gboolean		 is_local;
	gboolean		 loaded;
	gboolean		 enabled;
};

enum {
	PROP_0,
	PROP_LOADED,
	PROP_LAST
};

G_DEFINE_TYPE (ZifStore, zif_store, G_TYPE_OBJECT)

/**
 * zif_store_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_store_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_store_error");
	return quark;
}

/**
 * zif_store_add_package:
 * @store: A #ZifStore
 * @package: A #ZifPackage
 * @error: A #GError, or %NULL
 *
 * Adds a package to the store.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.6
 **/
gboolean
zif_store_add_package (ZifStore *store,
		       ZifPackage *package,
		       GError **error)
{
	gboolean ret = TRUE;
	GObject *package_tmp;

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check it's not already added */
	package_tmp = zif_array_lookup (store->priv->packages,
					G_OBJECT (package));
	if (package_tmp != NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "already added %s",
			     zif_package_get_printable (package));
		goto out;
	}

	/* just add */
	g_debug ("adding %s to %s",
		 zif_package_get_id (package),
		 zif_store_get_id (ZIF_STORE (store)));
	zif_array_add (store->priv->packages, G_OBJECT (package));
out:
	return ret;
}

/**
 * zif_store_add_packages:
 * @store: A #ZifStore
 * @array: Array of #ZifPackage's
 * @error: A #GError, or %NULL
 *
 * Adds an array of packages to the store.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.6
 **/
gboolean
zif_store_add_packages (ZifStore *store,
			GPtrArray *array,
			GError **error)
{
	gboolean ret = TRUE;
	ZifPackage *package;
	guint i;

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (array != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* just add */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_store_add_package (store, package, error);
		if (!ret)
			break;
	}
	return ret;
}

/**
 * zif_store_remove_package:
 * @store: A #ZifStore
 * @package: A #ZifPackage
 * @error: A #GError, or %NULL
 *
 * Removes a package from the store.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.6
 **/
gboolean
zif_store_remove_package (ZifStore *store,
			  ZifPackage *package,
			  GError **error)
{
	gboolean ret = TRUE;
	GObject *package_tmp;

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check it's not already removed */
	package_tmp = zif_array_lookup (store->priv->packages,
					G_OBJECT (package));
	if (package_tmp == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "package not found in array %s",
			     zif_package_get_printable (package));
		goto out;
	}

	/* just remove */
	g_debug ("removing %s from %s",
		 zif_package_get_id (ZIF_PACKAGE (package)),
		 zif_store_get_id (ZIF_STORE (store)));
	zif_array_remove (store->priv->packages, package_tmp);
out:
	return ret;
}

/**
 * zif_store_remove_packages:
 * @store: A #ZifStore
 * @array: Array of #ZifPackage's
 * @error: A #GError, or %NULL
 *
 * Removes an array of packages from the store.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.6
 **/
gboolean
zif_store_remove_packages (ZifStore *store,
			   GPtrArray *array,
			   GError **error)
{
	gboolean ret = TRUE;
	ZifPackage *package;
	guint i;

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (array != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* just remove */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_store_remove_package (store, package, error);
		if (!ret)
			break;
	}
	return ret;
}

/**
 * zif_store_load:
 * @store: A #ZifStore
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Loads the #ZifStore object.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_load (ZifStore *store, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (klass != NULL, FALSE);

	/* already loaded */
	if (store->priv->loaded) {
		ret = TRUE;
		goto out;
	}

	/* ensure any previous store is cleared */
	zif_array_empty (store->priv->packages);

	/* all superclasses must implement load */
	if (klass->load == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		goto out;
	}

	/* superclass */
	ret = klass->load (store, state, error);
	if (!ret)
		goto out;

	/* okay */
	store->priv->loaded = TRUE;
out:
	return ret;
}

/**
 * zif_store_unload:
 * @store: A #ZifStore
 * @error: A #GError, or %NULL
 *
 * Unloads the #ZifStore object.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.6
 **/
gboolean
zif_store_unload (ZifStore *store, GError **error)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* all superclasses must implement load */
	if (!store->priv->loaded) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_NO_SUPPORT,
				     "no loaded");
		goto out;
	}

	/* okay */
	store->priv->loaded = FALSE;
out:
	return ret;
}

/**
 * zif_store_clean:
 * @store: A #ZifStore
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Cleans the #ZifStore objects by deleting cache.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_clean (ZifStore *store, ZifState *state, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (klass != NULL, FALSE);

	/* superclass */
	if (klass->clean == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->clean (store, state, error);
}

/**
 * zif_store_refresh:
 * @store: A #ZifStore
 * @force: If the data should be re-downloaded if it's still valid
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * refresh the #ZifStore objects by downloading new data if required.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_refresh (ZifStore *store,
		   gboolean force,
		   ZifState *state,
		   GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (klass != NULL, FALSE);

	/* superclass */
	if (klass->refresh == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->refresh (store, force, state, error);
}

/**
 * zif_store_search_name:
 * @store: A #ZifStore
 * @search: A search term, e.g. "power"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that match the package name in some part.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_search_name (ZifStore *store,
		       gchar **search,
		       ZifState *state,
		       GError **error)
{
	const gchar *package_id;
	gboolean ret;
	gchar *split_name;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	guint i, j;
	ZifPackage *package;
	ZifState *state_local = NULL;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->search_name != NULL) {
		array = klass->search_name (store, search, state, error);
		goto out;
	}

	/* setup steps */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (store->priv->packages->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, store->priv->packages->len);

	/* iterate list */
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<store->priv->packages->len;i++) {
		package = ZIF_PACKAGE (zif_array_index (store->priv->packages, i));
		package_id = zif_package_get_id (package);
		split_name = zif_package_id_get_name (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcasestr (split_name, search[j]) != NULL) {
				g_ptr_array_add (array_tmp, g_object_ref (package));
				break;
			}
		}
		g_free (split_name);

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * zif_store_search_category:
 * @store: A #ZifStore
 * @search: A search term, e.g. "gnome/games"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Return packages in a specific category.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_search_category (ZifStore *store,
			   gchar **search,
			   ZifState *state,
			   GError **error)
{
	const gchar *category;
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	guint i, j;
	ZifPackage *package;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->search_category != NULL) {
		array = klass->search_category (store, search, state, error);
		goto out;
	}

	/* setup steps */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (store->priv->packages->len == 0) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, store->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<store->priv->packages->len;i++) {
		package = ZIF_PACKAGE (zif_array_index (store->priv->packages, i));
		state_loop = zif_state_get_child (state_local);
		category = zif_package_get_category (package, state_loop, NULL);
		for (j=0; search[j] != NULL; j++) {
			if (g_strcmp0 (category, search[j]) == 0) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}

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
	return array;
}

/**
 * zif_store_search_details:
 * @store: A #ZifStore
 * @search: A search term, e.g. "trouble"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that match some detail about the package.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_search_details (ZifStore *store,
			  gchar **search,
			  ZifState *state,
			  GError **error)
{
	const gchar *description;
	const gchar *package_id;
	gboolean ret;
	gchar *split_name;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	guint i, j;
	ZifPackage *package;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->search_details != NULL) {
		array = klass->search_details (store, search, state, error);
		goto out;
	}

	/* depending if we are loaded or not */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   10, /* load */
					   90, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (store->priv->packages->len == 0) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, store->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<store->priv->packages->len;i++) {
		package = ZIF_PACKAGE (zif_array_index (store->priv->packages, i));
		package_id = zif_package_get_id (package);
		state_loop = zif_state_get_child (state_local);
		description = zif_package_get_description (package, state_loop, NULL);
		split_name = zif_package_id_get_name (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcasestr (split_name, search[j]) != NULL) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			} else if (strcasestr (description, search[j]) != NULL) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}
		g_free (split_name);

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
	return array;
}

/**
 * zif_store_search_group:
 * @store: A #ZifStore
 * @search: A search term, e.g. "games"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that belong in a specific group.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_search_group (ZifStore *store,
			gchar **search,
			ZifState *state,
			GError **error)
{
	const gchar *group;
	const gchar *group_tmp;
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	guint i, j;
	ZifPackage *package;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->search_group != NULL) {
		array = klass->search_group (store, search, state, error);
		goto out;
	}

	/* setup steps */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (store->priv->packages->len == 0) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, store->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<store->priv->packages->len;i++) {
		package = ZIF_PACKAGE (zif_array_index (store->priv->packages, i));
		for (j=0; search[j] != NULL; j++) {
			group = search[j];
			state_loop = zif_state_get_child (state_local);
			group_tmp = zif_package_get_group (package, state_loop, NULL);
			if (g_strcmp0 (group, group_tmp) == 0) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}

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
	return array;
}

/**
 * zif_store_search_file:
 * @store: A #ZifStore
 * @search: A search term, e.g. "/usr/bin/gnome-power-manager"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that provide the specified file.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_search_file (ZifStore *store,
		       gchar **search,
		       ZifState *state,
		       GError **error)
{
	const gchar *filename;
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	GPtrArray *files;
	guint i, j, l;
	ZifPackage *package;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->search_file != NULL) {
		array = klass->search_file (store, search, state, error);
		goto out;
	}

	/* setup steps */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (store->priv->packages->len == 0) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}
	g_debug ("using %i local packages", store->priv->packages->len);

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, store->priv->packages->len);

	/* iterate list */
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<store->priv->packages->len;i++) {
		package = ZIF_PACKAGE (zif_array_index (store->priv->packages, i));
		state_loop = zif_state_get_child (state_local);
		files = zif_package_get_files (package, state_loop, &error_local);
		if (files == NULL) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to get file lists: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
		for (j=0; j<files->len; j++) {
			filename = g_ptr_array_index (files, j);
			for (l=0; search[l] != NULL; l++) {
				if (g_strcmp0 (search[l], filename) == 0) {
					g_ptr_array_add (array_tmp, g_object_ref (package));
					break;
				}
			}
		}
		g_ptr_array_unref (files);

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * zif_store_has_search_arch_suffix:
 **/
static gboolean
zif_store_has_search_arch_suffix (const gchar *search)
{
	if (g_str_has_suffix (search, ".noarch") ||
	    g_str_has_suffix (search, ".x86_64") ||
	    g_str_has_suffix (search, ".i386") ||
	    g_str_has_suffix (search, ".i486") ||
	    g_str_has_suffix (search, ".i586") ||
	    g_str_has_suffix (search, ".i686"))
		return TRUE;
	return FALSE;
}

/**
 * zif_store_resolve_full:
 * @store: A #ZifStore
 * @search: A search term, e.g. "gnome-power-manager.i386"
 * @flags: A bitfield of %ZifStoreResolveFlags, e.g. %ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds packages matching the package name exactly.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.2.4
 **/
GPtrArray *
zif_store_resolve_full (ZifStore *store,
			gchar **search,
			ZifStoreResolveFlags flags,
			ZifState *state,
			GError **error)
{
	const gchar *tmp;
	gboolean ret;
	gchar **search_native = NULL;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	guint i, j;
	ZifPackage *package;
	ZifState *state_local = NULL;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* do we want to prefer the native arch */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_PREFER_NATIVE) > 0 &&
	    ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH) > 0 ||
	     (flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION_ARCH) > 0)) {

		/* get the native machine arch */
		rpmGetArchInfo (&tmp, NULL);
		j = g_strv_length (search);
		search_native = g_new0 (gchar *, j + 1);
		for (i=0; i<j; i++) {
			if (zif_store_has_search_arch_suffix (search[i])) {
				search_native[i] = g_strdup (search[i]);
			} else {
				search_native[i] = g_strdup_printf ("%s.%s",
								    search[i],
								    tmp);
			}
		}
	} else {
		search_native = g_strdupv (search);
	}

	/* superclass */
	if (klass->resolve != NULL) {
		array = klass->resolve (store,
					search_native,
					flags,
					state,
					error);
		goto out;
	}

	/* setup steps */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (store->priv->packages->len == 0) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, store->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<store->priv->packages->len;i++) {
		package = ZIF_PACKAGE (zif_array_index (store->priv->packages, i));

		/* name */
		if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME) > 0) {
			tmp = zif_package_get_name (package);
			for (j=0; search_native[j] != NULL; j++) {
				if (strcmp (tmp, search_native[j]) == 0)
					g_ptr_array_add (array, g_object_ref (package));
			}
		}

		/* name.arch */
		if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH) > 0) {
			tmp = zif_package_get_name_arch (package);
			for (j=0; search_native[j] != NULL; j++) {
				if (strcmp (tmp, search_native[j]) == 0)
					g_ptr_array_add (array, g_object_ref (package));
			}
		}

		/* name-version */
		if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION) > 0) {
			tmp = zif_package_get_name_version (package);
			for (j=0; search_native[j] != NULL; j++) {
				if (strcmp (tmp, search_native[j]) == 0)
					g_ptr_array_add (array, g_object_ref (package));
			}
		}

		/* name-version.arch */
		if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION_ARCH) > 0) {
			tmp = zif_package_get_name_version_arch (package);
			for (j=0; search_native[j] != NULL; j++) {
				if (strcmp (tmp, search_native[j]) == 0)
					g_ptr_array_add (array, g_object_ref (package));
			}
		}

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
	g_strfreev (search_native);
	return array;
}

/**
 * zif_store_resolve:
 * @store: A #ZifStore
 * @search: A search term, e.g. "gnome-power-manager"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds packages matching the package name exactly.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_resolve (ZifStore *store,
		   gchar **search,
		   ZifState *state,
		   GError **error)
{
	return zif_store_resolve_full (store,
				       search,
				       ZIF_STORE_RESOLVE_FLAG_USE_NAME,
				       state,
				       error);
}

#if 0
/**
 * zif_store_what_depends:
 **/
static GPtrArray *
zif_store_what_depends (ZifStore *store,
			ZifPackageEnsureType type,
			GPtrArray *depends,
			ZifState *state,
			GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	GPtrArray *provides;
	guint i;
	guint j;
	guint k;
	ZifDepend *depend;
	ZifDepend *depend_tmp;
	ZifPackage *package;
	ZifState *state_local;

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* check we have packages */
	if (store->priv->packages->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "cannot resolve, no packages in meta sack");
		goto out;
	}

	/* we just search a virtual in-memory-sack */
	zif_state_set_number_steps (state, store->priv->packages->len);

	/* iterate list */
	array_tmp = zif_object_array_new ();
	for (i=0;i<store->priv->packages->len;i++) {
		package = ZIF_PACKAGE (zif_array_index (store->priv->packages, i));

		/* get package provides */
		state_local = zif_state_get_child (state);
		if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {
			provides = zif_package_get_provides (package,
							     state_local,
							     &error_local);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {
			provides = zif_package_get_requires (package,
							     state_local,
							     &error_local);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {
			provides = zif_package_get_conflicts (package,
							      state_local,
							      &error_local);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {
			provides = zif_package_get_obsoletes (package,
							      state_local,
							      &error_local);
		} else {
			g_assert_not_reached ();
		}
		if (provides == NULL) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to get provides/requires for %s: %s",
				     zif_package_get_printable (package),
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
		for (j=0; j<provides->len; j++) {
			depend_tmp = g_ptr_array_index (provides, j);
			for (k=0; k<depends->len; k++) {
				depend = g_ptr_array_index (depends, k);
				if (zif_depend_satisfies (depend_tmp, depend)) {
					zif_object_array_add (array_tmp, package);
					break;
				}
			}
		}
		g_ptr_array_unref (provides);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}
#endif

/**
 * zif_store_what_depends:
 **/
static GPtrArray *
zif_store_what_depends (ZifStore *store,
			ZifPackageEnsureType type,
			GPtrArray *depends,
			ZifState *state,
			GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	GPtrArray *array_pkgs = NULL;
	GPtrArray *depends_tmp;
	guint i;
	ZifDepend *depend_tmp;
	GError *error_local = NULL;
	gboolean ret;
	ZifState *state_local = NULL;

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup steps */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (store->priv->packages->len == 0) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* just use the helper function */
	state_local = zif_state_get_child (state);
	array_tmp = zif_object_array_new ();
	for (i=0; i<depends->len; i++) {
		depend_tmp = g_ptr_array_index (depends, i);
		array_pkgs = zif_array_get_array (store->priv->packages);
		if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {
			ret = zif_package_array_provide (array_pkgs,
							 depend_tmp, NULL,
							 &depends_tmp,
							 state_local,
							 error);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {
			ret = zif_package_array_require (array_pkgs,
							 depend_tmp, NULL,
							 &depends_tmp,
							 state_local,
							 error);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {
			ret = zif_package_array_conflict (array_pkgs,
							  depend_tmp, NULL,
							  &depends_tmp,
							  state_local,
							  error);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {
			ret = zif_package_array_obsolete (array_pkgs,
							  depend_tmp, NULL,
							  &depends_tmp,
							  state_local,
							  error);
		} else {
			g_assert_not_reached ();
		}
		g_ptr_array_unref (array_pkgs);
		if (!ret)
			goto out;

		/* add results */
		zif_object_array_add_array (array_tmp, depends_tmp);
		g_ptr_array_unref (depends_tmp);
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * zif_store_what_provides:
 * @store: A #ZifStore
 * @depends: An array of #ZifDepend's to search for
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that provide a specific string.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_store_what_provides (ZifStore *store, GPtrArray *depends, ZifState *state, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->what_provides != NULL) {
		return klass->what_provides (store, depends, state, error);
	}

	return zif_store_what_depends (store,
				       ZIF_PACKAGE_ENSURE_TYPE_PROVIDES,
				       depends,
				       state,
				       error);
}

/**
 * zif_store_what_requires:
 * @store: A #ZifStore
 * @depends: An array of #ZifDepend's to search for
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that provide a specific string.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_store_what_requires (ZifStore *store, GPtrArray *depends, ZifState *state, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->what_requires != NULL) {
		return klass->what_requires (store, depends, state, error);
	}

	return zif_store_what_depends (store,
				       ZIF_PACKAGE_ENSURE_TYPE_REQUIRES,
				       depends,
				       state,
				       error);
}

/**
 * zif_store_what_obsoletes:
 * @store: A #ZifStore
 * @depends: An array of #ZifDepend's to search for
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that obsolete a specific string.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_store_what_obsoletes (ZifStore *store, GPtrArray *depends, ZifState *state, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->what_obsoletes != NULL) {
		return klass->what_obsoletes (store, depends, state, error);
	}

	return zif_store_what_depends (store,
				       ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES,
				       depends,
				       state,
				       error);
}

/**
 * zif_store_what_conflicts:
 * @store: A #ZifStore
 * @depends: An array of #ZifDepend's to search for
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that conflict a specific string.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_store_what_conflicts (ZifStore *store, GPtrArray *depends, ZifState *state, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->what_conflicts != NULL) {
		return klass->what_conflicts (store, depends, state, error);
	}

	return zif_store_what_depends (store,
				       ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS,
				       depends,
				       state,
				       error);
}

/**
 * zif_store_get_packages:
 * @store: A #ZifStore
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Return all packages in the #ZifStore's.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_get_packages (ZifStore *store,
			ZifState *state,
			GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifState *state_local = NULL;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->get_packages != NULL) {
		array = klass->get_packages (store, state, error);
		goto out;
	}

	/* setup steps */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   99, /* load */
					   1, /* get copy */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* just get refcounted copy */
	array = zif_array_get_array (store->priv->packages);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_store_find_package:
 * @store: A #ZifStore
 * @package_id: A package ID which defines the package
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find a single package in the #ZifStore.
 *
 * Return value: A single #ZifPackage or %NULL. Use g_object_unref when done().
 *
 * Since: 0.1.0
 **/
ZifPackage *
zif_store_find_package (ZifStore *store,
			const gchar *package_id,
			ZifState *state,
			GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gpointer package_tmp;
	ZifPackage *package = NULL;
	ZifState *state_local = NULL;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (zif_package_id_check (package_id), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->find_package != NULL) {
		package = klass->find_package (store,
					       package_id,
					       state,
					       error);
		goto out;
	}

	/* setup steps */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_load (store,
				      state_local,
				      &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (store->priv->packages->len == 0) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* just do a hash lookup */
	package_tmp = zif_array_lookup_with_key (store->priv->packages,
						 package_id);
	if (package_tmp == NULL) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_TO_FIND,
				     "failed to find package");
		goto out;
	}

	/* return ref to package */
	package = g_object_ref (ZIF_PACKAGE (package_tmp));

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return package;
}

/**
 * zif_store_get_categories:
 * @store: A #ZifStore
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Return a list of custom categories.
 *
 * Return value: An array of #ZifCategory's, free with g_ptr_array_unref() when done.
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_get_categories (ZifStore *store, ZifState *state, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->get_categories == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->get_categories (store, state, error);
}

/**
 * zif_store_get_id:
 * @store: A #ZifStore
 *
 * Gets the id for the object.
 *
 * Return value: A text ID, or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
zif_store_get_id (ZifStore *store)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	/* superclass */
	if (klass->get_id == NULL)
		return NULL;

	return klass->get_id (store);
}

/**
 * zif_store_print:
 * @store: A #ZifStore
 *
 * Prints all the objects in the store.
 *
 * Since: 0.1.0
 **/
void
zif_store_print (ZifStore *store)
{
	guint i;
	ZifPackage *package;
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_if_fail (ZIF_IS_STORE (store));
	g_return_if_fail (klass != NULL);

	/* superclass */
	if (klass->print != NULL) {
		klass->print (store);
		return;
	}

	for (i=0;i<store->priv->packages->len;i++) {
		package = ZIF_PACKAGE (zif_array_index (store->priv->packages, i));
		zif_package_print (package);
	}
}

/**
 * zif_store_get_enabled:
 * @store: A #ZifStore
 *
 * Gets if the store is enabled at runtime.
 *
 * Since: 0.2.2
 **/
gboolean
zif_store_get_enabled (ZifStore *store)
{
	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	return store->priv->enabled;
}

/**
 * zif_store_set_enabled:
 * @store: A #ZifStore
 * @enabled: The new value
 *
 * Sets the store runtime enabled state.
 *
 * NOTE: this will not change results if the store has already been
 * referenced, but will stop the store showing up in the results from
 * zif_repos_get_stores_enabled().
 *
 * Since: 0.2.2
 **/
void
zif_store_set_enabled (ZifStore *store, gboolean enabled)
{
	g_return_if_fail (ZIF_IS_STORE (store));
	store->priv->enabled = enabled;
}

/**
 * zif_store_get_property:
 **/
static void
zif_store_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	ZifStore *store = ZIF_STORE (object);
	ZifStorePrivate *priv = store->priv;

	switch (prop_id) {
	case PROP_LOADED:
		g_value_set_boolean (value, priv->loaded);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_store_set_property:
 **/
static void
zif_store_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	ZifStore *store = ZIF_STORE (object);
	ZifStorePrivate *priv = store->priv;

	switch (prop_id) {
	case PROP_LOADED:
		priv->loaded = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_store_finalize:
 **/
static void
zif_store_finalize (GObject *object)
{
	ZifStore *store;
	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE (object));

	store = ZIF_STORE (object);
	g_object_unref (store->priv->packages);

	G_OBJECT_CLASS (zif_store_parent_class)->finalize (object);
}

/**
 * zif_store_class_init:
 **/
static void
zif_store_class_init (ZifStoreClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_store_finalize;
	object_class->get_property = zif_store_get_property;
	object_class->set_property = zif_store_set_property;

	/**
	 * ZifStore:loaded:
	 *
	 * Since: 0.2.3
	 */
	pspec = g_param_spec_boolean ("loaded", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LOADED, pspec);

	g_type_class_add_private (klass, sizeof (ZifStorePrivate));
}

/**
 * zif_store_init:
 **/
static void
zif_store_init (ZifStore *store)
{
	store->priv = ZIF_STORE_GET_PRIVATE (store);
	store->priv->packages = zif_array_new ();
	zif_array_set_mapping_func (store->priv->packages,
				    (ZifArrayMappingFuncCb) zif_package_get_id);
}

/**
 * zif_store_new:
 *
 * Return value: A new #ZifStore instance.
 *
 * Since: 0.1.0
 **/
ZifStore *
zif_store_new (void)
{
	ZifStore *store;
	store = g_object_new (ZIF_TYPE_STORE, NULL);
	return ZIF_STORE (store);
}

