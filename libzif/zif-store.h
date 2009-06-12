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

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_STORE_H
#define __ZIF_STORE_H

#include <glib-object.h>
#include <packagekit-glib/packagekit.h>

#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_STORE		(zif_store_get_type ())
#define ZIF_STORE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_STORE, ZifStore))
#define ZIF_STORE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_STORE, ZifStoreClass))
#define ZIF_IS_STORE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_STORE))
#define ZIF_IS_STORE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_STORE))
#define ZIF_STORE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_STORE, ZifStoreClass))

typedef struct ZifStorePrivate ZifStorePrivate;

typedef struct
{
	GObject			 parent;
	ZifStorePrivate		*priv;
} ZifStore;

typedef struct
{
	GObjectClass	parent_class;
	/* vtable */
	gboolean	 (*load)		(ZifStore		*store,
						 GError			**error);
	gboolean	 (*clean)		(ZifStore		*store,
						 GError			**error);
	GPtrArray	*(*search_name)		(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*search_category)	(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*search_details)	(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*search_group)	(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*search_file)		(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*resolve)		(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*what_provides)	(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*get_packages)	(ZifStore		*store,
						 GError			**error);
	GPtrArray	*(*get_updates)		(ZifStore		*store,
						 GError			**error);
	ZifPackage	*(*find_package)	(ZifStore		*store,
						 const PkPackageId	*id,
						 GError			**error);
	const gchar	*(*get_id)		(ZifStore		*store);
	void		 (*print)		(ZifStore		*store);
} ZifStoreClass;

GType		 zif_store_get_type		(void) G_GNUC_CONST;
ZifStore	*zif_store_new			(void);
gboolean	 zif_store_load			(ZifStore		*store,
						 GError			**error);
gboolean	 zif_store_clean		(ZifStore		*store,
						 GError			**error);
GPtrArray	*zif_store_search_name		(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_store_search_category	(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_store_search_details	(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_store_search_group		(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_store_search_file		(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_store_resolve		(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_store_what_provides	(ZifStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_store_get_packages		(ZifStore		*store,
						 GError			**error);
GPtrArray	*zif_store_get_updates		(ZifStore		*store,
						 GError			**error);
ZifPackage	*zif_store_find_package		(ZifStore		*store,
						 const PkPackageId	*id,
						 GError			**error);
const gchar	*zif_store_get_id		(ZifStore		*store);
void		 zif_store_print		(ZifStore		*store);

G_END_DECLS

#endif /* __ZIF_STORE_H */
