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

#include "dum-config.h"
#include "dum-store.h"
#include "dum-sack.h"
#include "dum-package.h"
#include "dum-utils.h"
#include "dum-repos.h"

#include "egg-debug.h"
#include "egg-string.h"

#define DUM_SACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_SACK, DumSackPrivate))

struct DumSackPrivate
{
	GPtrArray		*array;
};

G_DEFINE_TYPE (DumSack, dum_sack, G_TYPE_OBJECT)

/**
 * dum_sack_add_store:
 **/
gboolean
dum_sack_add_store (DumSack *sack, DumStore *store)
{
	g_return_val_if_fail (DUM_IS_SACK (sack), FALSE);
	g_return_val_if_fail (store != NULL, FALSE);

	g_ptr_array_add (sack->priv->array, g_object_ref (store));
	return TRUE;
}

/**
 * dum_sack_add_stores:
 **/
gboolean
dum_sack_add_stores (DumSack *sack, GPtrArray *stores)
{
	guint i;
	DumStore *store;
	gboolean ret = FALSE;

	g_return_val_if_fail (DUM_IS_SACK (sack), FALSE);
	g_return_val_if_fail (stores != NULL, FALSE);

	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);
		ret = dum_sack_add_store (sack, store);
		if (!ret)
			break;
	}
	return ret;
}

/**
 * dum_sack_repos_search:
 **/
static GPtrArray *
dum_sack_repos_search (DumSack *sack, PkRoleEnum role, const gchar *search, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	GPtrArray *stores;
	GPtrArray *part;
	DumStore *store;
	GError *error_local = NULL;

	/* find results in each store */
	stores = sack->priv->array;
	array = g_ptr_array_new ();
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);

		/* get results for this store */
		if (role == PK_ROLE_ENUM_RESOLVE)
			part = dum_store_resolve (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_NAME)
			part = dum_store_search_name (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_DETAILS)
			part = dum_store_search_details (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_GROUP)
			part = dum_store_search_group (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_FILE)
			part = dum_store_search_file (store, search, &error_local);
		else if (role == PK_ROLE_ENUM_GET_PACKAGES)
			part = dum_store_get_packages (store, &error_local);
		else if (role == PK_ROLE_ENUM_WHAT_PROVIDES)
			part = dum_store_what_provides (store, search, &error_local);
		else
			egg_error ("internal error: %s", pk_role_enum_to_text (role));
		if (part == NULL) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to %s in %s: %s", pk_role_enum_to_text (role), dum_store_get_id (store), error_local->message);
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
 * dum_sack_find_package:
 **/
DumPackage *
dum_sack_find_package (DumSack *sack, const PkPackageId *id, GError **error)
{
	guint i;
	GPtrArray *stores;
	DumStore *store;
	DumPackage *package = NULL;

	/* find results in each store */
	stores = sack->priv->array;
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);
		package = dum_store_find_package (store, id, NULL);
		if (package != NULL)
			break;
	}
	return package;
}

/**
 * dum_sack_resolve:
 **/
GPtrArray *
dum_sack_resolve (DumSack *sack, const gchar *search, GError **error)
{
	return dum_sack_repos_search (sack, PK_ROLE_ENUM_RESOLVE, search, error);
}

/**
 * dum_sack_search_name:
 **/
GPtrArray *
dum_sack_search_name (DumSack *sack, const gchar *search, GError **error)
{
	return dum_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_NAME, search, error);
}

/**
 * dum_sack_search_details:
 **/
GPtrArray *
dum_sack_search_details (DumSack *sack, const gchar *search, GError **error)
{
	return dum_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_DETAILS, search, error);
}

/**
 * dum_sack_search_group:
 **/
GPtrArray *
dum_sack_search_group (DumSack *sack, const gchar *search, GError **error)
{
	return dum_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_GROUP, search, error);
}

/**
 * dum_sack_search_file:
 **/
GPtrArray *
dum_sack_search_file (DumSack *sack, const gchar *search, GError **error)
{
	return dum_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_FILE, search, error);
}

/**
 * dum_sack_get_packages:
 **/
GPtrArray *
dum_sack_get_packages (DumSack *sack, GError **error)
{
	return dum_sack_repos_search (sack, PK_ROLE_ENUM_GET_PACKAGES, NULL, error);
}

/**
 * dum_sack_what_provides:
 **/
GPtrArray *
dum_sack_what_provides (DumSack *sack, const gchar *search, GError **error)
{
	/* if this is a path, then we use the file list and treat like a SearchFile */
	if (g_str_has_prefix (search, "/"))
		return dum_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_FILE, search, error);
	return dum_sack_repos_search (sack, PK_ROLE_ENUM_WHAT_PROVIDES, search, error);
}

/**
 * dum_sack_finalize:
 **/
static void
dum_sack_finalize (GObject *object)
{
	DumSack *sack;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_SACK (object));
	sack = DUM_SACK (object);

	g_ptr_array_foreach (sack->priv->array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (sack->priv->array, TRUE);

	G_OBJECT_CLASS (dum_sack_parent_class)->finalize (object);
}

/**
 * dum_sack_class_init:
 **/
static void
dum_sack_class_init (DumSackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_sack_finalize;
	g_type_class_add_private (klass, sizeof (DumSackPrivate));
}

/**
 * dum_sack_init:
 **/
static void
dum_sack_init (DumSack *sack)
{
	sack->priv = DUM_SACK_GET_PRIVATE (sack);
	sack->priv->array = g_ptr_array_new ();
}

/**
 * dum_sack_new:
 * Return value: A new sack class instance.
 **/
DumSack *
dum_sack_new (void)
{
	DumSack *sack;
	sack = g_object_new (DUM_TYPE_SACK, NULL);
	return DUM_SACK (sack);
}

