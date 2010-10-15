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
 * @short_description: A store-array is a container that holds one or more stores
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
#include "zif-utils.h"
#include "zif-repos.h"
#include "zif-category.h"

typedef enum {
	ZIF_ROLE_GET_PACKAGES,
	ZIF_ROLE_GET_UPDATES,
	ZIF_ROLE_RESOLVE,
	ZIF_ROLE_SEARCH_DETAILS,
	ZIF_ROLE_SEARCH_FILE,
	ZIF_ROLE_SEARCH_GROUP,
	ZIF_ROLE_SEARCH_NAME,
	ZIF_ROLE_SEARCH_CATEGORY,
	ZIF_ROLE_WHAT_PROVIDES,
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
	if (role == ZIF_ROLE_GET_UPDATES)
		return "get-updates";
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
	if (role == ZIF_ROLE_GET_CATEGORIES)
		return "get-categories";
	return NULL;
}

/**
 * zif_store_array_add_store:
 * @store_array: the #GPtrArray of #ZifStores
 * @store: the #ZifStore to add
 *
 * Add a single #ZifStore to the #GPtrArray.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_array_add_store (GPtrArray *store_array, ZifStore *store)
{
	g_return_val_if_fail (store != NULL, FALSE);

	g_ptr_array_add (store_array, g_object_ref (store));
	return TRUE;
}

/**
 * zif_store_array_add_stores:
 * @store_array: the #GPtrArray of #ZifStores
 * @stores: the array of #ZifStore's to add
 *
 * Add an array of #ZifStore's to the #GPtrArray.
 *
 * Return value: %TRUE for success, %FALSE for failure
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

	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);
		ret = zif_store_array_add_store (store_array, store);
		if (!ret)
			break;
	}
	return ret;
}

/**
 * zif_store_array_add_local:
 * @store_array: the #GPtrArray of #ZifStores
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Convenience function to add local store to the #GPtrArray.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_array_add_local (GPtrArray *store_array, ZifState *state, GError **error)
{
	ZifStoreLocal *store;

	store = zif_store_local_new ();
	zif_store_array_add_store (store_array, ZIF_STORE (store));
	g_object_unref (store);

	return TRUE;
}

/**
 * zif_store_array_add_remote:
 * @store_array: the #GPtrArray of #ZifStores
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Convenience function to add remote stores to the #GPtrArray.
 *
 * Return value: %TRUE for success, %FALSE for failure
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
 * @store_array: the #GPtrArray of #ZifStores
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Convenience function to add enabled remote stores to the #GPtrArray.
 *
 * Return value: %TRUE for success, %FALSE for failure
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
zif_store_array_repos_search (GPtrArray *store_array, ZifRole role, gchar **search,
			      ZifState *state, GError **error)
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
	for (i=0; i<store_array->len; i++) {
		store = g_ptr_array_index (store_array, i);

		/* create a chain of states */
		state_local = zif_state_get_child (state);

		/* get results for this store */
		if (role == ZIF_ROLE_RESOLVE)
			part = zif_store_resolve (store, search, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_NAME)
			part = zif_store_search_name (store, search, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_DETAILS)
			part = zif_store_search_details (store, search, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_GROUP)
			part = zif_store_search_group (store, search, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_CATEGORY)
			part = zif_store_search_category (store, search, state_local, &error_local);
		else if (role == ZIF_ROLE_SEARCH_FILE)
			part = zif_store_search_file (store, search, state_local, &error_local);
		else if (role == ZIF_ROLE_GET_PACKAGES)
			part = zif_store_get_packages (store, state_local, &error_local);
		else if (role == ZIF_ROLE_GET_UPDATES)
			part = zif_store_get_updates (store, (GPtrArray *) search, state_local, &error_local);
		else if (role == ZIF_ROLE_WHAT_PROVIDES)
			part = zif_store_what_provides (store, search, state_local, &error_local);
		else if (role == ZIF_ROLE_GET_CATEGORIES)
			part = zif_store_get_categories (store, state_local, &error_local);
		else {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "internal error, no such role: %s", zif_role_to_string (role));
			goto out;
		}
		if (part == NULL) {
			/* do we need to skip this error */
			ret = zif_state_error_handler (state, error_local);
			if (ret) {
				g_clear_error (&error_local);
				ret = zif_state_finished (state_local, error);
				if (!ret)
					goto out;
				goto skip_error;
			}
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to %s in %s: %s", zif_role_to_string (role), zif_store_get_id (store), error_local->message);
			g_error_free (error_local);
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
 * @store_array: the #GPtrArray of #ZifStores
 * @package_id: the PackageId which defines the package
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find a single package in the #GPtrArray.
 *
 * Return value: A single #ZifPackage or %NULL
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
	for (i=0; i<store_array->len; i++) {
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
 * @store_array: the #GPtrArray of #ZifStores
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Cleans the #ZifStoreRemote objects by deleting cache.
 *
 * Return value: %TRUE for success, %FALSE for failure
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

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (store_array->len == 0) {
		g_debug ("nothing to do");
		goto out;
	}

	/* set number of stores */
	zif_state_set_number_steps (state, store_array->len);

	/* do each one */
	for (i=0; i<store_array->len; i++) {
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
 * @store_array: the #GPtrArray of #ZifStores
 * @force: if the data should be re-downloaded if it's still valid
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Refreshs the #ZifStoreRemote objects by downloading new data
 *
 * Return value: %TRUE for success, %FALSE for failure
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

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (store_array->len == 0) {
		g_debug ("nothing to do");
		goto out;
	}

	/* create a chain of states */
	zif_state_set_number_steps (state, store_array->len);

	/* do each one */
	for (i=0; i<store_array->len; i++) {
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
 * zif_store_array_resolve:
 * @store_array: the #GPtrArray of #ZifStores
 * @search: the search term, e.g. "gnome-power-manager"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds packages matching the package name exactly.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_resolve (GPtrArray *store_array, gchar **search,
			 ZifState *state, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array, ZIF_ROLE_RESOLVE, search,
					     state, error);
}

/**
 * zif_store_array_search_name:
 * @store_array: the #GPtrArray of #ZifStores
 * @search: the search term, e.g. "power"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that match the package name in some part.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_search_name (GPtrArray *store_array, gchar **search,
			     ZifState *state, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array, ZIF_ROLE_SEARCH_NAME, search,
					     state, error);
}

/**
 * zif_store_array_search_details:
 * @store_array: the #GPtrArray of #ZifStores
 * @search: the search term, e.g. "trouble"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that match some detail about the package.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_search_details (GPtrArray *store_array, gchar **search,
				ZifState *state, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array, ZIF_ROLE_SEARCH_DETAILS, search,
					     state, error);
}

/**
 * zif_store_array_search_group:
 * @store_array: the #GPtrArray of #ZifStores
 * @group_enum: the group enumerated value, e.g. "games"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that belong in a specific group.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_search_group (GPtrArray *store_array, gchar **group_enum,
			      ZifState *state, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array, ZIF_ROLE_SEARCH_GROUP, group_enum,
					     state, error);
}

/**
 * zif_store_array_search_category:
 * @store_array: the #GPtrArray of #ZifStores
 * @group_id: the group id, e.g. "gnome-system-tools"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that belong in a specific category.
 *
 * Return value: an array of #ZifPackage's
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

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get all results from all repos */
	array = zif_store_array_repos_search (store_array, ZIF_ROLE_SEARCH_CATEGORY, group_id,
					      state, error);
	if (array == NULL)
		goto out;

	/* remove duplicate package_ids */
	for (i=0; i<array->len; i++) {
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
 * @store_array: the #GPtrArray of #ZifStores
 * @search: the search term, e.g. "/usr/bin/gnome-power-manager"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that provide the specified file.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_search_file (GPtrArray *store_array, gchar **search,
			     ZifState *state, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array, ZIF_ROLE_SEARCH_FILE, search,
					     state, error);
}

/**
 * zif_store_array_get_packages:
 * @store_array: the #GPtrArray of #ZifStores
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return all packages in the #GPtrArray's.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_get_packages (GPtrArray *store_array,
			      ZifState *state, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array, ZIF_ROLE_GET_PACKAGES, NULL,
					     state, error);
}

/**
 * zif_store_array_get_updates:
 * @store_array: the #GPtrArray of #ZifStores
 * @packages: the #GPtrArray of #ZifPackages to check for updates
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return a list of packages that are updatable.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_get_updates (GPtrArray *store_array, GPtrArray *packages,
			     ZifState *state, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return zif_store_array_repos_search (store_array, ZIF_ROLE_GET_UPDATES, (gchar **) packages,
					     state, error);
}

/**
 * zif_store_array_what_provides:
 * @store_array: the #GPtrArray of #ZifStores
 * @search: the search term, e.g. "gstreamer(codec-mp3)"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that provide a specific string.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_store_array_what_provides (GPtrArray *store_array, gchar **search,
				       ZifState *state, GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if this is a path, then we use the file list and treat like a SearchFile */
	if (g_str_has_prefix (search[0], "/")) {
		return zif_store_array_repos_search (store_array, ZIF_ROLE_SEARCH_FILE, search,
						     state, error);
	}
	return zif_store_array_repos_search (store_array, ZIF_ROLE_WHAT_PROVIDES, search,
					     state, error);
}

/**
 * zif_store_array_get_categories:
 * @store_array: the #GPtrArray of #ZifStores
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return a list of custom categories from all repos.
 *
 * Return value: an array of #ZifCategory's
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

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get all results from all repos */
	array = zif_store_array_repos_search (store_array, ZIF_ROLE_GET_CATEGORIES, NULL,
					      state, error);
	if (array == NULL)
		goto out;

	/* remove duplicate parents and groups */
	for (i=0; i<array->len; i++) {
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
 * zif_store_array_new:
 *
 * Return value: A new #GPtrArray class instance.
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

