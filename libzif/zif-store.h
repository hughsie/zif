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

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_STORE_H
#define __ZIF_STORE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "zif-depend.h"
#include "zif-package.h"
#include "zif-state.h"

G_BEGIN_DECLS

#define ZIF_TYPE_STORE		(zif_store_get_type ())
#define ZIF_STORE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_STORE, ZifStore))
#define ZIF_STORE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_STORE, ZifStoreClass))
#define ZIF_IS_STORE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_STORE))
#define ZIF_IS_STORE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_STORE))
#define ZIF_STORE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_STORE, ZifStoreClass))
#define ZIF_STORE_ERROR		(zif_store_error_quark ())

typedef struct _ZifStore	ZifStore;
typedef struct _ZifStorePrivate	ZifStorePrivate;
typedef struct _ZifStoreClass	ZifStoreClass;

struct _ZifStore
{
	GObject			 parent;
	ZifStorePrivate		*priv;
};
typedef enum {
	ZIF_STORE_RESOLVE_FLAG_USE_NAME			= 1<<0,
	ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH		= 1<<1,
	ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION		= 1<<2,
	ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION_ARCH	= 1<<3,
	ZIF_STORE_RESOLVE_FLAG_PREFER_NATIVE		= 1<<4,
	ZIF_STORE_RESOLVE_FLAG_USE_GLOB			= 1<<5,
	ZIF_STORE_RESOLVE_FLAG_USE_REGEX		= 1<<6,
} ZifStoreResolveFlags;

struct _ZifStoreClass
{
	GObjectClass	parent_class;
	/* vtable */
	gboolean	 (*load)		(ZifStore		*store,
						 ZifState		*state,
						 GError			**error);
	gboolean	 (*clean)		(ZifStore		*store,
						 ZifState		*state,
						 GError			**error);
	gboolean	 (*refresh)		(ZifStore		*store,
						 gboolean		 force,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_name)		(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_category)	(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_details)	(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_group)	(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_file)		(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*resolve)		(ZifStore		*store,
						 gchar			**search,
						 ZifStoreResolveFlags	 flags,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*what_provides)	(ZifStore		*store,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*what_requires)	(ZifStore		*store,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*what_obsoletes)	(ZifStore		*store,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*what_conflicts)	(ZifStore		*store,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*get_packages)	(ZifStore		*store,
						 ZifState		*state,
						 GError			**error);
	ZifPackage	*(*find_package)	(ZifStore		*store,
						 const gchar		*package_id,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*get_categories)	(ZifStore		*store,
						 ZifState		*state,
						 GError			**error);
	const gchar	*(*get_id)		(ZifStore		*store);
	void		 (*print)		(ZifStore		*store);
	/* TODO: next time we break API, add padding! */
};

typedef enum {
	ZIF_STORE_ERROR_FAILED,
	ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
	ZIF_STORE_ERROR_FAILED_TO_FIND,
	ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
	ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
	ZIF_STORE_ERROR_NO_SUPPORT,
	ZIF_STORE_ERROR_NOT_LOCKED,
	ZIF_STORE_ERROR_MULTIPLE_MATCHES,
	ZIF_STORE_ERROR_RECOVERABLE,
	ZIF_STORE_ERROR_LAST
} ZifStoreError;

/* just to avoid typing */
#define ZIF_STORE_RESOLVE_FLAG_USE_ALL	(ZIF_STORE_RESOLVE_FLAG_USE_NAME |		\
					 ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH |		\
					 ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION |	\
					 ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION_ARCH)

GType		 zif_store_get_type		(void);
GQuark		 zif_store_error_quark		(void);
ZifStore	*zif_store_new			(void);
gboolean	 zif_store_add_package		(ZifStore		*store,
						 ZifPackage		*package,
						 GError			**error);
gboolean	 zif_store_add_packages		(ZifStore		*store,
						 GPtrArray		*array,
						 GError			**error);
gboolean	 zif_store_remove_package	(ZifStore		*store,
						 ZifPackage		*package,
						 GError			**error);
gboolean	 zif_store_remove_packages	(ZifStore		*store,
						 GPtrArray		*array,
						 GError			**error);
gboolean	 zif_store_load			(ZifStore		*store,
						 ZifState		*state,
						 GError			**error);
gboolean	 zif_store_unload		(ZifStore		*store,
						 GError			**error);
gboolean	 zif_store_clean		(ZifStore		*store,
						 ZifState		*state,
						 GError			**error);
gboolean	 zif_store_refresh		(ZifStore		*store,
						 gboolean		 force,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_search_name		(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_search_category	(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_search_details	(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_search_group		(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_search_file		(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_resolve		(ZifStore		*store,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_resolve_full		(ZifStore		*store,
						 gchar			**search,
						 ZifStoreResolveFlags	 flags,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_what_provides	(ZifStore		*store,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_what_requires	(ZifStore		*store,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_what_obsoletes	(ZifStore		*store,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_what_conflicts	(ZifStore		*store,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_get_packages		(ZifStore		*store,
						 ZifState		*state,
						 GError			**error);
ZifPackage	*zif_store_find_package		(ZifStore		*store,
						 const gchar		*package_id,
						 ZifState		*state,
						 GError			**error);
GPtrArray	*zif_store_get_categories	(ZifStore		*store,
						 ZifState		*state,
						 GError			**error);
const gchar	*zif_store_get_id		(ZifStore		*store);
void		 zif_store_print		(ZifStore		*store);
gboolean	 zif_store_get_enabled		(ZifStore		*store);
void		 zif_store_set_enabled		(ZifStore		*store,
						 gboolean		 enabled);

G_END_DECLS

#endif /* __ZIF_STORE_H */

