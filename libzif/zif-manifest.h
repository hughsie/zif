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

#ifndef __ZIF_MANIFEST_H
#define __ZIF_MANIFEST_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ZIF_TYPE_MANIFEST		(zif_manifest_get_type ())
#define ZIF_MANIFEST(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MANIFEST, ZifManifest))
#define ZIF_MANIFEST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MANIFEST, ZifManifestClass))
#define ZIF_IS_MANIFEST(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MANIFEST))
#define ZIF_IS_MANIFEST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MANIFEST))
#define ZIF_MANIFEST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MANIFEST, ZifManifestClass))
#define ZIF_MANIFEST_ERROR		(zif_manifest_error_quark ())

typedef struct _ZifManifest		ZifManifest;
typedef struct _ZifManifestPrivate	ZifManifestPrivate;
typedef struct _ZifManifestClass	ZifManifestClass;

struct _ZifManifest
{
	GObject				 parent;
	ZifManifestPrivate		*priv;
};

struct _ZifManifestClass
{
	GObjectClass			 parent_class;
};

typedef enum {
	ZIF_MANIFEST_ERROR_FAILED,
	ZIF_MANIFEST_ERROR_POST_INSTALL,
	ZIF_MANIFEST_ERROR_LAST
} ZifManifestError;

GType		 zif_manifest_get_type			(void);
GQuark		 zif_manifest_error_quark		(void);
ZifManifest	*zif_manifest_new			(void);
gboolean	 zif_manifest_check			(ZifManifest	*manifest,
							 const gchar	*filename,
							 GError		**error);

G_END_DECLS

#endif /* __ZIF_MANIFEST_H */

