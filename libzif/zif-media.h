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

#ifndef __ZIF_MEDIA_H
#define __ZIF_MEDIA_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ZIF_TYPE_MEDIA		(zif_media_get_type ())
#define ZIF_MEDIA(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MEDIA, ZifMedia))
#define ZIF_MEDIA_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MEDIA, ZifMediaClass))
#define ZIF_IS_MEDIA(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MEDIA))
#define ZIF_IS_MEDIA_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MEDIA))
#define ZIF_MEDIA_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MEDIA, ZifMediaClass))

typedef struct _ZifMedia		ZifMedia;
typedef struct _ZifMediaPrivate		ZifMediaPrivate;
typedef struct _ZifMediaClass		ZifMediaClass;

struct _ZifMedia
{
	GObject			 parent;
	ZifMediaPrivate		*priv;
};

struct _ZifMediaClass
{
	GObjectClass		 parent_class;
	/* Padding for future expansion */
	void (*_zif_reserved1) (void);
	void (*_zif_reserved2) (void);
	void (*_zif_reserved3) (void);
	void (*_zif_reserved4) (void);
};

GType		 zif_media_get_type		(void);
ZifMedia	*zif_media_new			(void);
gchar		*zif_media_get_root_from_id	(ZifMedia	*media,
						 const gchar	*media_id);

G_END_DECLS

#endif /* __ZIF_MEDIA_H */
