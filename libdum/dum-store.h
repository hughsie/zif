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

#if !defined (__DUM_H_INSIDE__) && !defined (DUM_COMPILATION)
#error "Only <dum.h> can be included directly."
#endif

#ifndef __DUM_STORE_H
#define __DUM_STORE_H

#include <glib-object.h>
#include <packagekit-glib/packagekit.h>

#include "dum-package.h"

G_BEGIN_DECLS

#define DUM_TYPE_STORE		(dum_store_get_type ())
#define DUM_STORE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_STORE, DumStore))
#define DUM_STORE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_STORE, DumStoreClass))
#define DUM_IS_STORE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_STORE))
#define DUM_IS_STORE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_STORE))
#define DUM_STORE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_STORE, DumStoreClass))

typedef struct DumStorePrivate DumStorePrivate;

typedef struct
{
	GObject			 parent;
	DumStorePrivate		*priv;
} DumStore;

typedef struct
{
	GObjectClass	parent_class;
	/* vtable */
	gboolean	 (*load)		(DumStore		*store,
						 GError			**error);
	GPtrArray	*(*search_name)		(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*search_category)	(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*search_details)	(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*search_group)	(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*search_file)		(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*resolve)		(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*what_provides)	(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
	GPtrArray	*(*get_packages)	(DumStore		*store,
						 GError			**error);
	DumPackage	*(*find_package)	(DumStore		*store,
						 const PkPackageId	*id,
						 GError			**error);
	const gchar	*(*get_id)		(DumStore		*store);
	void		 (*print)		(DumStore		*store);
} DumStoreClass;

GType		 dum_store_get_type		(void) G_GNUC_CONST;
DumStore	*dum_store_new			(void);
gboolean	 dum_store_load			(DumStore		*store,
						 GError			**error);
GPtrArray	*dum_store_search_name		(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_store_search_category	(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_store_search_details	(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_store_search_group		(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_store_search_file		(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_store_resolve		(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_store_what_provides	(DumStore		*store,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_store_get_packages		(DumStore		*store,
						 GError			**error);
DumPackage	*dum_store_find_package		(DumStore		*store,
						 const PkPackageId	*id,
						 GError			**error);
const gchar	*dum_store_get_id		(DumStore		*store);
void		 dum_store_print		(DumStore		*store);

G_END_DECLS

#endif /* __DUM_STORE_H */

