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
 **/
ZifPackage *
zif_sack_find_package (ZifSack *sack, const PkPackageId *id, GError **error)
{
	guint i;
	GPtrArray *stores;
	ZifStore *store;
	ZifPackage *package = NULL;

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
 * zif_sack_resolve:
 **/
GPtrArray *
zif_sack_resolve (ZifSack *sack, const gchar *search, GError **error)
{
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_RESOLVE, search, error);
}

/**
 * zif_sack_search_name:
 **/
GPtrArray *
zif_sack_search_name (ZifSack *sack, const gchar *search, GError **error)
{
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_NAME, search, error);
}

/**
 * zif_sack_search_details:
 **/
GPtrArray *
zif_sack_search_details (ZifSack *sack, const gchar *search, GError **error)
{
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_DETAILS, search, error);
}

/**
 * zif_sack_search_group:
 **/
GPtrArray *
zif_sack_search_group (ZifSack *sack, const gchar *search, GError **error)
{
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_GROUP, search, error);
}

/**
 * zif_sack_search_file:
 **/
GPtrArray *
zif_sack_search_file (ZifSack *sack, const gchar *search, GError **error)
{
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_FILE, search, error);
}

/**
 * zif_sack_get_packages:
 **/
GPtrArray *
zif_sack_get_packages (ZifSack *sack, GError **error)
{
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_GET_PACKAGES, NULL, error);
}

/**
 * zif_sack_what_provides:
 **/
GPtrArray *
zif_sack_what_provides (ZifSack *sack, const gchar *search, GError **error)
{
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
 * Return value: A new sack class instance.
 **/
ZifSack *
zif_sack_new (void)
{
	ZifSack *sack;
	sack = g_object_new (ZIF_TYPE_SACK, NULL);
	return ZIF_SACK (sack);
}

