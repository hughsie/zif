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
 * remote or local source. It can be thought of as an in-memory store.
 *
 * A #ZifStoreMeta is a subclassed #ZifStore and operates on packages.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>

#include <glib.h>

#include "zif-store.h"
#include "zif-store-meta.h"

#define ZIF_STORE_META_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_META, ZifStoreMetaPrivate))

struct _ZifStoreMetaPrivate
{
	gboolean		 is_local;
};

G_DEFINE_TYPE (ZifStoreMeta, zif_store_meta, ZIF_TYPE_STORE)

/**
 * zif_store_meta_set_is_local:
 * @store: A #ZifStoreMeta
 * @is_local: %TRUE if this is a local repo
 *
 * This function changes no results, it just changes the repository
 * identifier to be "meta-local" rather than "meta".
 *
 * Since: 0.1.3
 **/
void
zif_store_meta_set_is_local (ZifStoreMeta *store, gboolean is_local)
{
	g_return_if_fail (ZIF_IS_STORE_META (store));
	store->priv->is_local = is_local;
}

/**
 * zif_store_meta_get_id:
 **/
static const gchar *
zif_store_meta_get_id (ZifStore *store)
{
	ZifStoreMeta *meta = ZIF_STORE_META (store);
	g_return_val_if_fail (ZIF_IS_STORE_META (store), NULL);
	if (meta->priv->is_local)
		return "meta-local";
	return "meta-remote";
}

/**
 * zif_store_meta_load:
 **/
static gboolean
zif_store_meta_load (ZifStore *store, ZifState *state, GError **error)
{
	return TRUE;
}

/**
 * zif_store_meta_finalize:
 **/
static void
zif_store_meta_finalize (GObject *object)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_META (object));
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
	store_class->get_id = zif_store_meta_get_id;
	store_class->load = zif_store_meta_load;

	g_type_class_add_private (klass, sizeof (ZifStoreMetaPrivate));
}

/**
 * zif_store_meta_init:
 **/
static void
zif_store_meta_init (ZifStoreMeta *store)
{
	store->priv = ZIF_STORE_META_GET_PRIVATE (store);
}

/**
 * zif_store_meta_new:
 *
 * Return value: A new #ZifStoreMeta instance.
 *
 * Since: 0.1.3
 **/
ZifStore *
zif_store_meta_new (void)
{
	ZifStoreMeta *store_meta;
	store_meta = g_object_new (ZIF_TYPE_STORE_META,
				   "loaded", TRUE,
				   NULL);
	return ZIF_STORE (store_meta);
}

