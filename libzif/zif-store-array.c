/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-store-array
 * @short_description: Container for one or more stores
 *
 * A #GPtrArray is the container where #ZifStore's are kept. Global operations can
 * be done on the array and not the indervidual stores.
 *
 * IMPORTANT: any errors that happen on the ZifStores are fatal unless you're
 * using zif_state_set_error_handler().
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-config.h"
#include "zif-state.h"
#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-store-array.h"
#include "zif-package.h"
#include "zif-package-array.h"
#include "zif-package-remote.h"
#include "zif-utils.h"
#include "zif-repos.h"
#include "zif-category.h"
#include "zif-object-array.h"

typedef enum {
	ZIF_ROLE_GET_PACKAGES,
	ZIF_ROLE_RESOLVE,
	ZIF_ROLE_SEARCH_DETAILS,
	ZIF_ROLE_SEARCH_FILE,
	ZIF_ROLE_SEARCH_GROUP,
	ZIF_ROLE_SEARCH_NAME,
	ZIF_ROLE_SEARCH_CATEGORY,
	ZIF_ROLE_WHAT_PROVIDES,
	ZIF_ROLE_WHAT_OBSOLETES,
	ZIF_ROLE_WHAT_CONFLICTS,
	ZIF_ROLE_WHAT_REQUIRES,
	ZIF_ROLE_GET_CATEGORIES,
	ZIF_ROLE_UNKNOWN
} ZifRole;

/**
 * zif_role_to_string:
 **/
static const gchar *
zif_role_to_string (ZifRole role)
{
	if (role == ZIF_ROLE_GET_PACKAGES)
		return "get-packages";
	if (role == ZIF_ROLE_RESOLVE)
		return "resolve";
	if (role == ZIF_ROLE_SEARCH_DETAILS)
		return "search-details";
	if (role == ZIF_ROLE_SEARCH_FILE)
		return "search-file";
	if (role == ZIF_ROLE_SEARCH_GROUP)
		return "search-group";
	if (role == ZIF_ROLE_SEARCH_NAME)
		return "search-name";
	if (role == ZIF_ROLE_SEARCH_CATEGORY)
		return "search-category";
	if (role == ZIF_ROLE_WHAT_PROVIDES)
		return "what-provides";
	if (role == ZIF_ROLE_WHAT_REQUIRES)
		return "what-requires";
	if (role == ZIF_ROLE_WHAT_OBSOLETES)
		return "what-obsoletes";
	if (role == ZIF_ROLE_WHAT_CONFLICTS)
		return "what-conflicts";
	if (role == ZIF_ROLE_GET_CATEGORIES)
		return "get-categories";
	return NULL;
}

/**
 * zif_store_array_find_by_id:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @id: The ID of the #ZifStore, e.g. 'fedora-debuginfo'
 *
 * Finds a single #ZifStore in the #GPtrArray.
 *
 * Return value: (transfer none): a %ZifStore for success, %NULL otherwise
 *
 * Since: 0.3.4
 **/
ZifStore *
zif_store_array_find_by_id (GPtrArray *store_array, const gchar *id)
{
	guint i;
	ZifStore *store_tmp;

	/* O(n) */
	for (i = 0; i < store_array->len; i++) {
		store_tmp = g_ptr_array_index (store_array, i);
		if (g_strcmp0 (zif_store_get_id (store_tmp), id) == 0)
			return store_tmp;
	}
	return NULL;
}

/**
 * zif_store_array_add_store:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @store: A #ZifStore to add
 *
 * Add a single #ZifStore to the #GPtrArray if it does not already
 * exist.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_array_add_store (GPtrArray *store_array, ZifStore *store)
{
	g_return_val_if_fail (store != NULL, FALSE);

	/* does already exist in the store */
	if (zif_store_array_find_by_id (store_array, zif_store_get_id (store)) != NULL)
		return FALSE;
	g_ptr_array_add (store_array, g_object_ref (store));
	return TRUE;
}

/**
 * zif_store_array_add_stores:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @stores: An a rray of #ZifStore's to add
 *
 * Add an array of #ZifStore's to the #GPtrArray.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_array_add_stores (GPtrArray *store_array, GPtrArray *stores)
{
	guint i;
	ZifStore *store;
	gboolean ret = FALSE;

	g_return_val_if_fail (stores != NULL, FALSE);

	for (i = 0; i < stores->len; i++) {
		store = g_ptr_array_index (stores, i);
		ret = zif_store_array_add_store (store_array, store);
		if (!ret)
			break;
	}
	return ret;
}

/**
 * zif_store_array_add_local:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Convenience function to add local store to the #GPtrArray.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_array_add_local (GPtrArray *store_array, ZifState *state, GError **error)
{
	ZifStore *store;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	store = zif_store_local_new ();
	zif_store_array_add_store (store_array, store);
	g_object_unref (store);

	return TRUE;
}

/**
 * zif_store_array_add_remote:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Convenience function to add remote stores to the #GPtrArray.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_array_add_remote (GPtrArray *store_array, ZifState *state, GError **error)
{
	GPtrArray *array;
	ZifRepos *repos;
	GError *error_local = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* get stores */
	repos = zif_repos_new ();
	array = zif_repos_get_stores (repos, state, &error_local);
	if (array == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get enabled stores: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* add */
	zif_store_array_add_stores (store_array, array);

	/* free */
	g_ptr_array_unref (array);
out:
	g_object_unref (repos);
	return ret;
}

/**
 * zif_store_array_add_remote_enabled:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Convenience function to add enabled remote stores to the #GPtrArray.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_array_add_remote_enabled (GPtrArray *store_array, ZifState *state, GError **error)
{
	GPtrArray *array;
	ZifRepos *repos;
	GError *error_local = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* get stores */
	repos = zif_repos_new ();
	array = zif_repos_get_stores_enabled (repos, state, &error_local);
	if (array == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get enabled stores: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* add */
	zif_store_array_add_stores (store_array, array);

	/* free */
	g_ptr_array_unref (array);
out:
	g_object_unref (repos);
	return ret;
}

/**
 * zif_store_array_repos_search:
 **/
static GPtrArray *
zif_store_array_repos_search (GPtrArray *store_array,
			      ZifRole role,
			      gpointer search,
			      guint flags,
			      ZifState *state,
			      GError **error)
{
	gboolean ret;
	guint i, j;
	GPtrArray *array = NULL;
	GPtrArray *array_results = NULL;
	GPtrArray *part;
	ZifStore *store;
	ZifPackage *package;
	GError *error_local = NULL;
	ZifState *state_local = NULL;

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* nothing to do */
	if (store_array->len == 0) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
			     "nothing to do as no stores in store_array");
		goto out;
	}

	/* set number of stores */
	zif_state_set_number_steps (state, store_array->len);

	/* do each one */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i = 0; i < store_array->len; i++) {
		store = g_ptr_array_index (store_array, i);

		/* we disabled this store? */
		if (!zif_store_get_enabled (store))
			goto skip_error;

		/* create a chain of states */
		state_local = zif_state_get_child (state);

		/* get results for this store */
		if (role == ZIF_ROLE_RESOLVE)
			part = zif_store_resolve_full (store, (gchar**)search, flags, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_NAME)
			part = zif_store_search_name (store, (gchar**)search, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_DETAILS)
			part = zif_store_search_details (store, (gchar**)search, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_GROUP)
			part = zif_store_search_group (store, (gchar**)search, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_CATEGORY)
			part = zif_store_search_category (store, (gchar**)search, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_FILE)
			part = zif_store_search_file (store, (gchar**)search, state_local, &error_local);
		else if (role == ZIF_ROLE_GET_PACKAGES)
			part = zif_store_get_packages (store, state_local, &error_local);
		else if (role == ZIF_ROLE_WHAT_PROVIDES)
			part = zif_store_what_provides (store, (GPtrArray*) search, state_local, &error_local);
		else if (role == ZIF_ROLE_WHAT_REQUIRES)
			part = zif_store_what_requires (store, (GPtrArray*) search, state_local, &error_local);
		else if (role == ZIF_ROLE_WHAT_OBSOLETES)
			part = zif_store_what_obsoletes (store, (GPtrArray*) search, state_local, &error_local);
		else if (role == ZIF_ROLE_WHAT_CONFLICTS)
			part = zif_store_what_conflicts (store, (GPtrArray*) search, state_local, &error_local);
		else if (role == ZIF_ROLE_GET_CATEGORIES)
			part = zif_store_get_categories (store, state_local, &error_local);
		else {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "internal error, no such role: %s", zif_role_to_string (role));
			goto out;
		}
		if (part == NULL) {

			/* the store get disabled whilst being used */
			if (g_error_matches (error_local,
					     ZIF_STORE_ERROR,
					     ZIF_STORE_ERROR_NOT_ENABLED)) {
				g_debug ("repo %s disabled whilst being used: %s",
					 zif_store_get_id (store),
					 error_local->message);
				g_clear_error (&error_local);
				ret = zif_state_finished (state_local, error);
				if (!ret)
					goto out;
				goto skip_error;
			}

			/* do we need to skip this error */
			ret = zif_state_error_handler (state, error_local);
			if (ret) {
				g_clear_error (&error_local);
				ret = zif_state_finished (state_local, error);
				if (!ret)
					goto out;
				goto skip_error;
			}
			g_propagate_prefixed_error (error,
						    error_local,
						    "failed to %s in %s: ",
						    zif_role_to_string (role),
						    zif_store_get_id (store));
			goto out;
		}

		for (j=0; j<part->len; j++) {
			package = g_ptr_array_index (part, j);
			g_ptr_array_add (array, g_object_ref (package));
		}
		g_ptr_array_unref (part);
skip_error:
		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* we're done */
	array_results = g_ptr_array_ref (array);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return array_results;
}

/**
 * zif_store_array_find_package:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @package_id: A PackageId which defines the package
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find a single package in the #GPtrArray.
 *
 * Return value: (transfer full): A single #ZifPackage or %NULL
 *
 * Since: 0.1.0
 **/
ZifPackage *
zif_store_array_find_package (GPtrArray *store_array, const gchar *package_id, ZifState *state, GError **error)
{
	guint i;
	gboolean ret;
	ZifStore *store;
	ZifPackage *package = NULL;
	GError *error_local = NULL;
	ZifState *state_local = NULL;

	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* nothing to do */
	if (store_array->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "package cannot be found as the store array is empty");
		goto out;
	}

	/* create a chain of states */
	zif_state_set_number_steps (state, store_array->len);

	/* do each one */
	for (i = 0; i < store_array->len; i++) {
		store = g_ptr_array_index (store_array, i);

		state_local = zif_state_get_child (state);
		package = zif_store_find_package (store, package_id, state_local, &error_local);

		/* get results */
		if (package == NULL) {
			if (error_local->code == ZIF_STORE_ERROR_FAILED_TO_FIND) {
				/* do not abort */
				g_clear_error (&error_local);
				ret = zif_state_finished (state_local, error);
				if (!ret)
					goto out;
			} else {
				g_set_error (error, 1, 0, "failed to find package: %s", error_local->message);
				g_error_free (error_local);
				goto out;
			}
		} else {
			ret = zif_state_finished (state, error);
			if (!ret)
				goto out;
			break;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* nothing to do */
	if (package == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "package cannot be found");
		goto out;
	}
out:
	return package;
}

/**
 * zif_store_array_clean:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Cleans the #ZifStoreRemote objects by deleting cache.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_array_clean (GPtrArray *store_array,
		       ZifState *state, GError **error)
{
	guint i;
	ZifStore *store;
	gboolean ret = TRUE;
	GError *error_local = NULL;
	ZifState *state_local = NULL;

	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (store_array->len == 0) {
		g_debug ("nothing to do");
		goto out;
	}

	/* set number of stores */
	zif_state_set_number_steps (state, store_array->len);

	/* do each one */
	for (i = 0; i < store_array->len; i++) {
		store = g_ptr_array_index (store_array, i);

		/* clean this one */
		state_local = zif_state_get_child (state);
		ret = zif_store_clean (store, state_local, &error_local);
		if (!ret) {
			/* do we need to skip this error */
			if (zif_state_error_handler (state, error_local)) {
				g_clear_error (&error_local);
				ret = zif_state_finished (state_local, error);
				if (!ret)
					goto out;
				goto skip_error;
			}
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to clean %s: %s", zif_store_get_id (store), error_local->message);
			g_error_free (error_local);
			goto out;
		}
skip_error:
		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * zif_store_array_refresh:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @force: if the data should be re-downloaded if it's still valid
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Refreshes the #ZifStoreRemote objects by downloading new data
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_array_refresh (GPtrArray *store_array, gboolean force,
			 ZifState *state, GError **error)
{
	guint i;
	ZifStore *store;
	gboolean ret = TRUE;
	GError *error_local = NULL;
	ZifState *state_local = NULL;

	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (store_array->len == 0) {
		g_debug ("nothing to do");
		goto out;
	}

	/* create a chain of states */
	zif_state_set_number_steps (state, store_array->len);

	/* do each one */
	for (i = 0; i < store_array->len; i++) {
		store = g_ptr_array_index (store_array, i);

		/* refresh this one */
		state_local = zif_state_get_child (state);
		ret = zif_store_refresh (store, force, state_local, &error_local);
		if (!ret) {
			/* do we need to skip this error */
			if (zif_state_error_handler (state, error_local)) {
				g_clear_error (&error_local);
				ret = zif_state_finished (state_local, error);
				if (!ret)
					goto out;
				goto skip_error;
			}
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to clean %s: %s", zif_store_get_id (store), error_local->message);
			g_error_free (error_local);
			goto out;
		}
skip_error:
		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * zif_store_array_resolve_full:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @search: (array zero-terminated=1) (element-type utf8): The search terms, e.g. "gnome-power-manager"
 * @flags: A bitfield of %ZifStoreResolveFlags, e.g. %ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds packages matching the package in a certain way, for instance
 * matching the name, the name.arch or even the name-version depending
 * on the flags used..
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.2.4
 **/
GPtrArray *
zif_store_array_resolve_full (GPtrArray *store_array,
			      gchar **search,
			      ZifStoreResolveFlags flags,
			      ZifState *state,
			      GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_RESOLVE,
					     search,
					     flags,
					     state,
					     error);
}

/**
 * zif_store_array_resolve:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @search: (array zero-terminated=1) (element-type utf8): The search terms, e.g. "gnome-power-manager"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds packages matching the package name exactly.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_resolve (GPtrArray *store_array, gchar **search,
			 ZifState *state, GError **error)
{
	return zif_store_array_resolve_full (store_array,
					     search,
					     ZIF_STORE_RESOLVE_FLAG_USE_NAME,
					     state,
					     error);
}

/**
 * zif_store_array_search_name:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @search: (array zero-terminated=1) (element-type utf8): The search terms, e.g. "power"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that match the package name in some part.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_search_name (GPtrArray *store_array, gchar **search,
			     ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_SEARCH_NAME,
					     search,
					     0, /* flags */
					     state,
					     error);
}

/**
 * zif_store_array_search_details:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @search: (array zero-terminated=1) (element-type utf8): The search terms, e.g. "trouble"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that match some detail about the package.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_search_details (GPtrArray *store_array, gchar **search,
				ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_SEARCH_DETAILS,
					     search,
					     0, /* flags */
					     state,
					     error);
}

/**
 * zif_store_array_search_group:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @group_enum: The group enumerated values, e.g. "games"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that belong in a specific group.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_search_group (GPtrArray *store_array, gchar **group_enum,
			      ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_SEARCH_GROUP,
					     group_enum,
					     0, /* flags */
					     state,
					     error);
}

/**
 * zif_store_array_search_category:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @group_id: A group id, e.g. "gnome-system-tools"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that belong in a specific category.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_search_category (GPtrArray *store_array, gchar **group_id,
					 ZifState *state, GError **error)
{
	guint i, j;
	GPtrArray *array;
	ZifPackage *package;
	const gchar *package_id;
	const gchar *package_id_tmp;
	gchar **split;

	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get all results from all repos */
	array = zif_store_array_repos_search (store_array,
					      ZIF_ROLE_SEARCH_CATEGORY,
					      group_id,
					      0, /* flags */
					      state,
					      error);
	if (array == NULL)
		goto out;

	/* remove duplicate package_ids */
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		package_id = zif_package_get_id (package);
		for (j=0; j<array->len; j++) {
			if (i == j)
				continue;
			package = g_ptr_array_index (array, j);
			package_id_tmp = zif_package_get_id (package);
			if (g_strcmp0 (package_id, package_id_tmp) == 0) {
				split = zif_package_id_split (package_id);
				/* duplicate */
				g_ptr_array_remove_index (array, j);
				g_strfreev (split);
			}
		}
	}
out:
	return array;
}

/**
 * zif_store_array_search_file:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @search: (array zero-terminated=1) (element-type utf8): The search terms, e.g. "/usr/bin/gnome-power-manager"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that provide the specified file.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_search_file (GPtrArray *store_array, gchar **search,
			     ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_SEARCH_FILE,
					     search,
					     0, /* flags */
					     state,
					     error);
}

/**
 * zif_store_array_get_packages:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Return all packages in the #GPtrArray's.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_get_packages (GPtrArray *store_array,
			      ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_GET_PACKAGES,
					     NULL,
					     0, /* flags */
					     state,
					     error);
}

/**
 * zif_store_array_what_provides:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @depends: A #ZifDepend to search for
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that provide a specific string.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_store_array_what_provides (GPtrArray *store_array, GPtrArray *depends,
			       ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_WHAT_PROVIDES,
					     depends,
					     0, /* flags */
					     state,
					     error);
}

/**
 * zif_store_array_what_requires:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @depends: A #ZifDepend to search for
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that require a specific string.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_store_array_what_requires (GPtrArray *store_array, GPtrArray *depends,
			       ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_WHAT_REQUIRES,
					     depends,
					     0, /* flags */
					     state,
					     error);
}

/**
 * zif_store_array_what_obsoletes:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @depends: A #ZifDepend to search for
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that conflict with a specific string.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_store_array_what_obsoletes (GPtrArray *store_array, GPtrArray *depends,
				ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_WHAT_OBSOLETES,
					     depends,
					     0, /* flags */
					     state,
					     error);
}

/**
 * zif_store_array_what_conflicts:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @depends: A #ZifDepend to search for
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find packages that conflict with a specific string.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of #ZifPackage's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_store_array_what_conflicts (GPtrArray *store_array, GPtrArray *depends,
				ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return zif_store_array_repos_search (store_array,
					     ZIF_ROLE_WHAT_CONFLICTS,
					     depends,
					     0, /* flags */
					     state,
					     error);
}

/**
 * zif_store_array_get_categories:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Return a list of custom categories from all repos.
 *
 * Return value: (element-type ZifCategory) (transfer container): An array of #ZifCategory's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_get_categories (GPtrArray *store_array,
				ZifState *state, GError **error)
{
	guint i, j;
	GPtrArray *array;
	ZifCategory *obj;
	ZifCategory *obj_tmp;
	gchar *parent_id;
	gchar *parent_id_tmp;
	gchar *cat_id;
	gchar *cat_id_tmp;

	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get all results from all repos */
	array = zif_store_array_repos_search (store_array,
					      ZIF_ROLE_GET_CATEGORIES,
					      NULL,
					      0, /* flags */
					      state,
					      error);
	if (array == NULL)
		goto out;

	/* remove duplicate parents and groups */
	for (i = 0; i < array->len; i++) {
		obj = g_ptr_array_index (array, i);
		g_object_get (obj,
			      "parent-id", &parent_id,
			      "cat-id", &cat_id,
			      NULL);
		for (j=0; j<array->len; j++) {
			if (i == j)
				continue;
			obj_tmp = g_ptr_array_index (array, j);
			g_object_get (obj_tmp,
				      "parent-id", &parent_id_tmp,
				      "cat-id", &cat_id_tmp,
				      NULL);
			if (g_strcmp0 (parent_id_tmp, parent_id) == 0 &&
			    g_strcmp0 (cat_id_tmp, cat_id) == 0) {
				/* duplicate */
				g_ptr_array_remove_index (array, j);
			}
			g_free (parent_id_tmp);
			g_free (cat_id_tmp);
		}
		g_free (parent_id);
		g_free (cat_id);
	}
out:
	return array;
}

/**
 * zif_store_array_get_updates:
 * @store_array: (element-type ZifStore): An array of #ZifStores
 * @store_local: The #ZifStoreLocal to use for the installed packages
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the list of packages that can be updated to newer versions.
 *
 * Return value: (element-type ZifPackage) (transfer container): An array of the *new* #ZifPackage's, not the existing
 * installed packages that are going to be updated.
 *
 * Note: this is a convenience function which makes a few assumptions.
 *
 * Since: 0.2.1
 **/
GPtrArray *
zif_store_array_get_updates (GPtrArray *store_array,
			     ZifStore *store_local,
			     ZifState *state,
			     GError **error)
{
	gboolean ret = FALSE;
	gchar *archinfo = NULL;
	gchar **search = NULL;
	gint val;
	GPtrArray *array_installed = NULL;
	GPtrArray *array_obsoletes = NULL;
	GPtrArray *depend_array = NULL;
	GPtrArray *retval = NULL;
	GPtrArray *updates_available = NULL;
	GPtrArray *updates = NULL;
	guint i;
	guint j;
	ZifConfig *config = NULL;
	ZifDepend *depend;
	ZifPackage *package;
	ZifPackage *update;
	ZifState *state_local;

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (state,
				   error,
				   5, /* get local packages */
				   5, /* filter newest */
				   10, /* resolve local list to remote */
				   10, /* add obsoletes */
				   70, /* filter out anything not newer */
				   -1);
	if (!ret)
		goto out;

	/* set state */
	zif_state_action_start (state,
				ZIF_STATE_ACTION_CHECKING_UPDATES,
				NULL);

	/* get installed packages */
	state_local = zif_state_get_child (state);
	array_installed = zif_store_get_packages (store_local,
						  state_local,
						  error);
	if (array_installed == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* remove any packages that are not newest (think kernel) */
	zif_package_array_filter_newest (array_installed);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* resolve each one remote */
	search = g_new0 (gchar *, array_installed->len + 1);
	for (i = 0; i < array_installed->len; i++) {
		package = g_ptr_array_index (array_installed, i);
		search[i] = g_strdup (zif_package_get_name (package));
	}
	state_local = zif_state_get_child (state);
	updates = zif_store_array_resolve (store_array,
					   search,
					   state_local,
					   error);
	if (updates == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* some repos contain lots of versions of one package */
	zif_package_array_filter_newest (updates);

	/* find each one in a remote repo */
	updates_available = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i = 0; i < array_installed->len; i++) {
		package = ZIF_PACKAGE (g_ptr_array_index (array_installed, i));

		/* find updates */
		for (j=0; j<updates->len; j++) {
			update = ZIF_PACKAGE (g_ptr_array_index (updates, j));

			/* newer? */
			val = zif_package_compare (update, package);
			if (val == G_MAXINT)
				continue;

			/* arch okay, add to list */
			if (val > 0) {
				g_debug ("*** update %s from %s.%s to %s.%s",
					 zif_package_get_name (package),
					 zif_package_get_version (package),
					 zif_package_get_arch (package),
					 zif_package_get_version (update),
					 zif_package_get_arch (update));
				g_ptr_array_add (updates_available,
						 g_object_ref (update));

				/* ensure the remote package knows about
				 * the installed version so we can
				 * calculate the delta */
				if (ZIF_IS_PACKAGE_REMOTE (update)) {
					zif_package_remote_set_installed (ZIF_PACKAGE_REMOTE (update),
									  package);
				}
				break;
			}
		}
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* add obsoletes */
	depend_array = zif_object_array_new ();
	for (i = 0; i < array_installed->len; i++) {
		package = ZIF_PACKAGE (g_ptr_array_index (array_installed, i));
		depend = zif_depend_new_from_values (zif_package_get_name (package),
						     ZIF_DEPEND_FLAG_EQUAL,
						     zif_package_get_version (package));
		zif_object_array_add (depend_array, depend);
		g_object_unref (depend);
	}

	/* find if anything obsoletes these */
	state_local = zif_state_get_child (state);
	array_obsoletes = zif_store_array_what_obsoletes (store_array,
							  depend_array,
							  state_local,
							  error);
	if (array_obsoletes == NULL)
		goto out;
	for (j=0; j<array_obsoletes->len; j++) {
		update = ZIF_PACKAGE (g_ptr_array_index (array_obsoletes, j));
		g_debug ("*** obsolete %s",
			 zif_package_get_printable (update));
	}

	/* filter by best architecture, as obsoletes do not have an arch */
	config = zif_config_new ();
	archinfo = zif_config_get_string (config,
					  "archinfo",
					  error);
	if (archinfo == NULL)
		goto out;
	zif_package_array_filter_best_arch (array_obsoletes, archinfo);

	/* add obsolete array to updates */
	zif_object_array_add_array (updates_available, array_obsoletes);
	zif_package_array_filter_duplicates (updates_available);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	retval = g_ptr_array_ref (updates_available);
out:
	g_free (archinfo);
	g_strfreev (search);
	if (config != NULL)
		g_object_unref (config);
	if (depend_array != NULL)
		g_ptr_array_unref (depend_array);
	if (array_obsoletes != NULL)
		g_ptr_array_unref (array_obsoletes);
	if (array_installed != NULL)
		g_ptr_array_unref (array_installed);
	if (updates != NULL)
		g_ptr_array_unref (updates);
	if (updates_available != NULL)
		g_ptr_array_unref (updates_available);
	return retval;
}

/**
 * zif_store_array_new:
 *
 * Return value: (element-type ZifStore) (transfer container): A new #GPtrArray instance.
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_new (void)
{
	GPtrArray *store_array;
	store_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	return store_array;
}

