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

#ifndef __ZIF_PACKAGE_META_H
#define __ZIF_PACKAGE_META_H

#include <glib-object.h>

#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_PACKAGE_META		(zif_package_meta_get_type ())
#define ZIF_PACKAGE_META(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_PACKAGE_META, ZifPackageMeta))
#define ZIF_PACKAGE_META_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_PACKAGE_META, ZifPackageMetaClass))
#define ZIF_IS_PACKAGE_META(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_PACKAGE_META))
#define ZIF_IS_PACKAGE_META_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_PACKAGE_META))
#define ZIF_PACKAGE_META_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_PACKAGE_META, ZifPackageMetaClass))

typedef struct _ZifPackageMeta		ZifPackageMeta;
typedef struct _ZifPackageMetaPrivate	ZifPackageMetaPrivate;
typedef struct _ZifPackageMetaClass	ZifPackageMetaClass;

struct _ZifPackageMeta
{
	ZifPackage		 parent;
	ZifPackageMetaPrivate	*priv;
};

struct _ZifPackageMetaClass
{
	ZifPackageClass		 parent_class;
};

GType			 zif_package_meta_get_type		(void);
ZifPackage		*zif_package_meta_new			(void);
gboolean		 zif_package_meta_set_from_filename	(ZifPackageMeta *pkg,
								 const gchar	*filename,
								 GError		**error)
								 G_GNUC_WARN_UNUSED_RESULT;
gboolean		 zif_package_meta_set_from_data		(ZifPackageMeta *pkg,
								 gchar		**data);

G_END_DECLS

#endif /* __ZIF_PACKAGE_META_H */

