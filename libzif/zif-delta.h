/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either checksum 2 of the License, or
 * (at your option) any later checksum.
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

#ifndef __ZIF_DELTA_H
#define __ZIF_DELTA_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define ZIF_TYPE_DELTA		(zif_delta_get_type ())
#define ZIF_DELTA(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_DELTA, ZifDelta))
#define ZIF_DELTA_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_DELTA, ZifDeltaClass))
#define ZIF_IS_DELTA(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_DELTA))
#define ZIF_IS_DELTA_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_DELTA))
#define ZIF_DELTA_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_DELTA, ZifDeltaClass))
#define ZIF_DELTA_ERROR		(zif_delta_error_quark ())

typedef struct _ZifDelta		 ZifDelta;
typedef struct _ZifDeltaPrivate	 ZifDeltaPrivate;
typedef struct _ZifDeltaClass	 ZifDeltaClass;

struct _ZifDelta
{
	GObject			 parent;
	ZifDeltaPrivate	*priv;
};

struct _ZifDeltaClass
{
	GObjectClass		 parent_class;
};

GType			 zif_delta_get_type		(void);
ZifDelta		*zif_delta_new			(void);

const gchar		*zif_delta_get_id		(ZifDelta		*delta);
guint64			 zif_delta_get_size		(ZifDelta		*delta);
const gchar		*zif_delta_get_filename		(ZifDelta		*delta);
const gchar		*zif_delta_get_sequence		(ZifDelta		*delta);
const gchar		*zif_delta_get_checksum		(ZifDelta		*delta);

G_END_DECLS

#endif /* __ZIF_DELTA_H */

