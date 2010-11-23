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

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_ARRAY_H
#define __ZIF_ARRAY_H

#include <glib-object.h>

#include "zif-array.h"

G_BEGIN_DECLS

#define ZIF_TYPE_ARRAY		(zif_array_get_type ())
#define ZIF_ARRAY(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_ARRAY, ZifArray))
#define ZIF_ARRAY_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_ARRAY, ZifArrayClass))
#define ZIF_IS_ARRAY(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_ARRAY))
#define ZIF_IS_ARRAY_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_ARRAY))
#define ZIF_ARRAY_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_ARRAY, ZifArrayClass))
#define ZIF_ARRAY_ERROR		(zif_array_error_quark ())

typedef struct _ZifArray	ZifArray;
typedef struct _ZifArrayPrivate	ZifArrayPrivate;
typedef struct _ZifArrayClass	ZifArrayClass;

struct _ZifArray
{
	GObject			 parent;
	guint			 len;
	ZifArrayPrivate		*priv;
};

struct _ZifArrayClass
{
	GObjectClass	parent_class;
};

GType		 zif_array_get_type		(void);
ZifArray	*zif_array_new			(void);

typedef const gchar *(*ZifArrayMappingFuncCb)	(GObject		*object);

gboolean	 zif_array_add			(ZifArray		*array,
						 gpointer		 data);
gboolean	 zif_array_remove		(ZifArray		*array,
						 gpointer		 data);
gboolean	 zif_array_remove_with_key	(ZifArray		*array,
						 const gchar		*key);
GObject		*zif_array_lookup		(ZifArray		*array,
						 gpointer		 data);
GObject		*zif_array_lookup_with_key	(ZifArray		*array,
						 const gchar		*key);
GObject		*zif_array_index		(ZifArray		*array,
						 guint			 index);
void		 zif_array_set_mapping_func	(ZifArray		*array,
						 ZifArrayMappingFuncCb	 mapping_func);
GPtrArray	*zif_array_get_array		(ZifArray		*array);

G_END_DECLS

#endif /* __ZIF_ARRAY_H */

