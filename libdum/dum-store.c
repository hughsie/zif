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

#include "dum-store.h"
#include "dum-package.h"

#include "egg-debug.h"
#include "egg-string.h"

G_DEFINE_TYPE (DumStore, dum_store, G_TYPE_OBJECT)

/**
 * dum_store_load:
 **/
gboolean
dum_store_load (DumStore *store, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);

	/* no support */
	if (klass->load == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->load (store, error);
}

/**
 * dum_store_search_name:
 **/
GPtrArray *
dum_store_search_name (DumStore *store, const gchar *search, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);
	g_return_val_if_fail (search != NULL, NULL);

	/* no support */
	if (klass->search_name == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->search_name (store, search, error);
}

/**
 * dum_store_search_category:
 **/
GPtrArray *
dum_store_search_category (DumStore *store, const gchar *search, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);
	g_return_val_if_fail (search != NULL, NULL);

	/* no support */
	if (klass->search_category == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->search_category (store, search, error);
}

/**
 * dum_store_earch_details:
 **/
GPtrArray *
dum_store_search_details (DumStore *store, const gchar *search, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);
	g_return_val_if_fail (search != NULL, NULL);

	/* no support */
	if (klass->search_details == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->search_details (store, search, error);
}

/**
 * dum_store_search_group:
 **/
GPtrArray *
dum_store_search_group (DumStore *store, const gchar *search, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);
	g_return_val_if_fail (search != NULL, NULL);

	/* no support */
	if (klass->search_group == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->search_group (store, search, error);
}

/**
 * dum_store_search_file:
 **/
GPtrArray *
dum_store_search_file (DumStore *store, const gchar *search, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);
	g_return_val_if_fail (search != NULL, NULL);

	/* no support */
	if (klass->search_file == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->search_file (store, search, error);
}

/**
 * dum_store_resolve:
 **/
GPtrArray *
dum_store_resolve (DumStore *store, const gchar *search, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);
	g_return_val_if_fail (search != NULL, NULL);

	/* no support */
	if (klass->resolve == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->resolve (store, search, error);
}

/**
 * dum_store_what_provides:
 **/
GPtrArray *
dum_store_what_provides (DumStore *store, const gchar *search, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);
	g_return_val_if_fail (search != NULL, NULL);

	/* no support */
	if (klass->search_name == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->what_provides (store, search, error);
}

/**
 * dum_store_get_packages:
 **/
GPtrArray *
dum_store_get_packages (DumStore *store, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);

	/* no support */
	if (klass->get_packages == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->get_packages (store, error);
}

/**
 * dum_store_find_package:
 **/
DumPackage *
dum_store_find_package (DumStore *store, const PkPackageId *id, GError **error)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), FALSE);
	g_return_val_if_fail (id != NULL, NULL);

	/* no support */
	if (klass->find_package == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->find_package (store, id, error);
}

/**
 * dum_store_get_id:
 **/
const gchar *
dum_store_get_id (DumStore *store)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_val_if_fail (DUM_IS_STORE (store), NULL);

	/* no support */
	if (klass->get_id == NULL)
		return NULL;

	return klass->get_id (store);
}

/**
 * dum_store_print:
 **/
void
dum_store_print (DumStore *store)
{
	DumStoreClass *klass = DUM_STORE_GET_CLASS (store);

	g_return_if_fail (DUM_IS_STORE (store));

	/* no support */
	if (klass->print == NULL)
		return;

	klass->print (store);
}

/**
 * dum_store_finalize:
 **/
static void
dum_store_finalize (GObject *object)
{
	DumStore *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_STORE (object));
	store = DUM_STORE (object);

	G_OBJECT_CLASS (dum_store_parent_class)->finalize (object);
}

/**
 * dum_store_class_init:
 **/
static void
dum_store_class_init (DumStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_store_finalize;
}

/**
 * dum_store_init:
 **/
static void
dum_store_init (DumStore *store)
{
}

/**
 * dum_store_new:
 * Return value: A new store class instance.
 **/
DumStore *
dum_store_new (void)
{
	DumStore *store;
	store = g_object_new (DUM_TYPE_STORE, NULL);
	return DUM_STORE (store);
}

