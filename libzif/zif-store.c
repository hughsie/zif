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

#include "zif-store.h"
#include "zif-package.h"

#include "egg-debug.h"
#include "egg-string.h"

G_DEFINE_TYPE (ZifStore, zif_store, G_TYPE_OBJECT)

/**
 * zif_store_load:
 **/
gboolean
zif_store_load (ZifStore *store, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);

	/* no support */
	if (klass->load == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->load (store, error);
}

/**
 * zif_store_search_name:
 **/
GPtrArray *
zif_store_search_name (ZifStore *store, const gchar *search, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
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
 * zif_store_search_category:
 **/
GPtrArray *
zif_store_search_category (ZifStore *store, const gchar *search, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
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
 * zif_store_earch_details:
 **/
GPtrArray *
zif_store_search_details (ZifStore *store, const gchar *search, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
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
 * zif_store_search_group:
 **/
GPtrArray *
zif_store_search_group (ZifStore *store, const gchar *search, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
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
 * zif_store_search_file:
 **/
GPtrArray *
zif_store_search_file (ZifStore *store, const gchar *search, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
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
 * zif_store_resolve:
 **/
GPtrArray *
zif_store_resolve (ZifStore *store, const gchar *search, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
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
 * zif_store_what_provides:
 **/
GPtrArray *
zif_store_what_provides (ZifStore *store, const gchar *search, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
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
 * zif_store_get_packages:
 **/
GPtrArray *
zif_store_get_packages (ZifStore *store, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);

	/* no support */
	if (klass->get_packages == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->get_packages (store, error);
}

/**
 * zif_store_get_updates:
 **/
GPtrArray *
zif_store_get_updates (ZifStore *store, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);

	/* no support */
	if (klass->get_updates == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->get_updates (store, error);
}

/**
 * zif_store_find_package:
 **/
ZifPackage *
zif_store_find_package (ZifStore *store, const PkPackageId *id, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
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
 * zif_store_get_id:
 **/
const gchar *
zif_store_get_id (ZifStore *store)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);

	/* no support */
	if (klass->get_id == NULL)
		return NULL;

	return klass->get_id (store);
}

/**
 * zif_store_print:
 **/
void
zif_store_print (ZifStore *store)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_if_fail (ZIF_IS_STORE (store));

	/* no support */
	if (klass->print == NULL)
		return;

	klass->print (store);
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

	G_OBJECT_CLASS (zif_store_parent_class)->finalize (object);
}

/**
 * zif_store_class_init:
 **/
static void
zif_store_class_init (ZifStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_store_finalize;
}

/**
 * zif_store_init:
 **/
static void
zif_store_init (ZifStore *store)
{
}

/**
 * zif_store_new:
 * Return value: A new store class instance.
 **/
ZifStore *
zif_store_new (void)
{
	ZifStore *store;
	store = g_object_new (ZIF_TYPE_STORE, NULL);
	return ZIF_STORE (store);
}

