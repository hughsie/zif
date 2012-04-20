/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_STORE_DIRECTORY_H
#define __ZIF_STORE_DIRECTORY_H

#include <glib-object.h>

#include "zif-store.h"
#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_STORE_DIRECTORY		(zif_store_directory_get_type ())
#define ZIF_STORE_DIRECTORY(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_STORE_DIRECTORY, ZifStoreDirectory))
#define ZIF_STORE_DIRECTORY_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_STORE_DIRECTORY, ZifStoreDirectoryClass))
#define ZIF_IS_STORE_DIRECTORY(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_STORE_DIRECTORY))
#define ZIF_IS_STORE_DIRECTORY_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_STORE_DIRECTORY))
#define ZIF_STORE_DIRECTORY_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_STORE_DIRECTORY, ZifStoreDirectoryClass))

typedef struct _ZifStoreDirectory		ZifStoreDirectory;
typedef struct _ZifStoreDirectoryPrivate	ZifStoreDirectoryPrivate;
typedef struct _ZifStoreDirectoryClass		ZifStoreDirectoryClass;

struct _ZifStoreDirectory
{
	ZifStore			 parent;
	ZifStoreDirectoryPrivate	*priv;
};

struct _ZifStoreDirectoryClass
{
	ZifStoreClass			 parent_class;
	/* Padding for future expansion */
	void (*_zif_reserved1) (void);
	void (*_zif_reserved2) (void);
	void (*_zif_reserved3) (void);
	void (*_zif_reserved4) (void);
};

GType		 zif_store_directory_get_type	(void);
ZifStore	*zif_store_directory_new	(void);
gboolean	 zif_store_directory_set_path	(ZifStoreDirectory	*store,
						 const gchar		*path,
						 gboolean		 recursive,
						 GError			**error);
const gchar	*zif_store_directory_get_path	(ZifStoreDirectory	*store);

G_END_DECLS

#endif /* __ZIF_STORE_DIRECTORY_H */

