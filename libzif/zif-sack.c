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

#include <glib.h>
#include <packagekit-glib/packagekit.h>

#include "zif-config.h"
#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-sack.h"
#include "zif-package.h"
#include "zif-utils.h"
#include "zif-repos.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_SACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_SACK, ZifSackPrivate))

struct ZifSackPrivate
{
	GPtrArray		*array;
};

G_DEFINE_TYPE (ZifSack, zif_sack, G_TYPE_OBJECT)

/**
 * zif_sack_add_store:
 * @sack: the #ZifSack object
 * @store: the #ZifStore to add
 *
 * Add a single #ZifStore to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_store (ZifSack *sack, ZifStore *store)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);
	g_return_val_if_fail (store != NULL, FALSE);

	g_ptr_array_add (sack->priv->array, g_object_ref (store));
	return TRUE;
}

/**
 * zif_sack_add_stores:
 * @sack: the #ZifSack object
 * @stores: the array of #ZifStore's to add
 *
 * Add an array of #ZifStore's to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_stores (ZifSack *sack, GPtrArray *stores)
{
	guint i;
	ZifStore *store;
	gboolean ret = FALSE;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);
	g_return_val_if_fail (stores != NULL, FALSE);

	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);
		ret = zif_sack_add_store (sack, store);
		if (!ret)
			break;
	}
	return ret;
}

/**
 * zif_sack_add_local:
 * @sack: the #ZifSack object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Convenience function to add local store to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_local (ZifSack *sack, GError **error)
{
	ZifStoreLocal *store;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);

	store = zif_store_local_new ();
	zif_sack_add_store (sack, ZIF_STORE (store));
	g_object_unref (store);

	return TRUE;
}

/**
 * zif_sack_add_remote:
 * @sack: the #ZifSack object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Convenience function to add remote stores to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_remote (ZifSack *sack, GError **error)
{
	GPtrArray *array;
	ZifRepos *repos;
	GError *error_local = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);

	/* get stores */
	repos = zif_repos_new ();
	array = zif_repos_get_stores (repos, &error_local);
	if (array == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get enabled stores: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* add */
	zif_sack_add_stores (ZIF_SACK (sack), array);

	/* free */
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);
out:
	g_object_unref (repos);
	return TRUE;
}

/**
 * zif_sack_add_remote_enabled:
 * @sack: the #ZifSack object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Convenience function to add enabled remote stores to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_remote_enabled (ZifSack *sack, GError **error)
{
	GPtrArray *array;
	ZifRepos *repos;
	GError *error_local = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);

	/* get stores */
	repos = zif_repos_new ();
	array = zif_repos_get_stores_enabled (repos, &error_local);
	if (array == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get enabled stores: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* add */
	zif_sack_add_stores (ZIF_SACK (sack), array);

	/* free */
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);
out:
	g_object_unref (repos);
	return TRUE;
}

/**
 * zif_sack_repos_search:
 **/
static GPtrArray *
zif_sack_repos_search (ZifSack *sack, PkRoleEnum role, const gchar *search, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	GPtrArray *stores;
	GPtrArray *part;
	ZifStore *store;
	GError *error_local = NULL;

	/* find results in each store */
	stores = sack->priv->array;
	array = g_ptr_array_new ();
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);

		/* get results for this store */
		if (role == PK_ROLE_ENUM_RESOLVE)
			part = zif_store_resolve (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_NAME)
			part = zif_store_search_name (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_DETAILS)
			part = zif_store_search_details (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_GROUP)
			part = zif_store_search_group (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_FILE)
			part = zif_store_search_file (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_GET_PACKAGES)
			part = zif_store_get_packages (store, &error_local);
		else if (role == PK_ROLE_ENUM_GET_UPDATES)
			part = zif_store_get_updates (store, &error_local);
		else if (role == PK_ROLE_ENUM_WHAT_PROVIDES)
			part = zif_store_what_provides (store, search, &error_local);
		else
			egg_error ("internal error: %s", pk_role_enum_to_text (role));
		if (part == NULL) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to %s in %s: %s", pk_role_enum_to_text (role), zif_store_get_id (store), error_local->message);
			g_error_free (error_local);
			g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
			g_ptr_array_free (array, TRUE);
			array = NULL;
			goto out;
		}

		for (j=0; j<part->len; j++)
			g_ptr_array_add (array, g_ptr_array_index (part, j));
		g_ptr_array_free (part, TRUE);
	}
out:
	return array;
}

/**
 * zif_sack_find_package:
 * @sack: the #ZifSack object
 * @id: the #PkPackageId which defines the package
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find a single package in the #ZifSack.
 *
 * Return value: A single #ZifPackage or %NULL
 **/
ZifPackage *
zif_sack_find_package (ZifSack *sack, const PkPackageId *id, GError **error)
{
	guint i;
	GPtrArray *stores;
	ZifStore *store;
	ZifPackage *package = NULL;

	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);

	/* find results in each store */
	stores = sack->priv->array;
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);
		package = zif_store_find_package (store, id, NULL);
		if (package != NULL)
			break;
	}
	return package;
}

/**
 * zif_sack_clean:
 * @sack: the #ZifSack object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Cleans the #ZifStoreRemote objects by deleting cache.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_clean (ZifSack *sack, GError **error)
{
	guint i;
	GPtrArray *stores;
	ZifStore *store;
	gboolean ret = TRUE;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);

	/* clean each store */
	stores = sack->priv->array;
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);

		/* clean this one */
		ret = zif_store_clean (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to clean %s: %s", zif_store_get_id (store), error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	return ret;
}

/**
 * zif_sack_resolve:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "gnome-power-manager"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds packages matching the package name exactly.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_resolve (ZifSack *sack, const gchar *search, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_RESOLVE, search, error);
}

/**
 * zif_sack_search_name:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "power"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that match the package name in some part.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_search_name (ZifSack *sack, const gchar *search, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_NAME, search, error);
}

/**
 * zif_sack_search_details:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "trouble"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that match some detail about the package.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_search_details (ZifSack *sack, const gchar *search, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_DETAILS, search, error);
}

/**
 * zif_sack_search_group:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "games"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that belong in a specific group.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_search_group (ZifSack *sack, const gchar *search, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_GROUP, search, error);
}

/**
 * zif_sack_search_file:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "/usr/bin/gnome-power-manager"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that provide the specified file.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_search_file (ZifSack *sack, const gchar *search, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_FILE, search, error);
}

/**
 * zif_sack_get_packages:
 * @sack: the #ZifSack object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return all packages in the #ZifSack's.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_get_packages (ZifSack *sack, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_GET_PACKAGES, NULL, error);
}

/**
 * zif_sack_get_updates:
 * @sack: the #ZifSack object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return a list of packages that are updatable.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_get_updates (ZifSack *sack, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_GET_UPDATES, NULL, error);
}

/**
 * zif_sack_what_provides:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "gstreamer(codec-mp3)"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that provide a specific string.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_what_provides (ZifSack *sack, const gchar *search, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);

	/* if this is a path, then we use the file list and treat like a SearchFile */
	if (g_str_has_prefix (search, "/"))
		return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_FILE, search, error);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_WHAT_PROVIDES, search, error);
}

/**
 * zif_sack_finalize:
 **/
static void
zif_sack_finalize (GObject *object)
{
	ZifSack *sack;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_SACK (object));
	sack = ZIF_SACK (object);

	g_ptr_array_foreach (sack->priv->array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (sack->priv->array, TRUE);

	G_OBJECT_CLASS (zif_sack_parent_class)->finalize (object);
}

/**
 * zif_sack_class_init:
 **/
static void
zif_sack_class_init (ZifSackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_sack_finalize;
	g_type_class_add_private (klass, sizeof (ZifSackPrivate));
}

/**
 * zif_sack_init:
 **/
static void
zif_sack_init (ZifSack *sack)
{
	sack->priv = ZIF_SACK_GET_PRIVATE (sack);
	sack->priv->array = g_ptr_array_new ();
}

/**
 * zif_sack_new:
 * Return value: A new #ZifSack class instance.
 **/
ZifSack *
zif_sack_new (void)
{
	ZifSack *sack;
	sack = g_object_new (ZIF_TYPE_SACK, NULL);
	return ZIF_SACK (sack);
}

