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

#ifndef __ZIF_SACK_H
#define __ZIF_SACK_H

#include <glib-object.h>

#include "zif-store.h"
#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_SACK		(zif_sack_get_type ())
#define ZIF_SACK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_SACK, ZifSack))
#define ZIF_SACK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_SACK, ZifSackClass))
#define ZIF_IS_SACK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_SACK))
#define ZIF_IS_SACK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_SACK))
#define ZIF_SACK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_SACK, ZifSackClass))

typedef struct ZifSackPrivate ZifSackPrivate;

typedef struct
{
	GObject			 parent;
	ZifSackPrivate	*priv;
} ZifSack;

typedef struct
{
	GObjectClass	parent_class;
} ZifSackClass;

GType		 zif_sack_get_type		(void) G_GNUC_CONST;
ZifSack		*zif_sack_new			(void);

/* stores */
gboolean	 zif_sack_add_store		(ZifSack		*sack,
						 ZifStore		*store);
gboolean	 zif_sack_add_stores		(ZifSack		*sack,
						 GPtrArray		*stores);

/* methods */
GPtrArray	*zif_sack_resolve		(ZifSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_sack_search_name		(ZifSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_sack_search_details	(ZifSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_sack_search_group		(ZifSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_sack_search_file		(ZifSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_sack_what_provides		(ZifSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*zif_sack_get_packages		(ZifSack		*sack,
						 GError			**error);
ZifPackage	*zif_sack_find_package		(ZifSack		*sack,
						 const PkPackageId	*id,
						 GError			**error);

G_END_DECLS

#endif /* __ZIF_SACK_H */

