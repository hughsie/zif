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

#ifndef __ZIF_DB_H
#define __ZIF_DB_H

#include <glib-object.h>

#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_DB		(zif_db_get_type ())
#define ZIF_DB(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_DB, ZifDb))
#define ZIF_DB_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_DB, ZifDbClass))
#define ZIF_IS_DB(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_DB))
#define ZIF_IS_DB_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_DB))
#define ZIF_DB_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_DB, ZifDbClass))
#define ZIF_DB_ERROR		(zif_db_error_quark ())

typedef struct _ZifDb		ZifDb;
typedef struct _ZifDbPrivate	ZifDbPrivate;
typedef struct _ZifDbClass	ZifDbClass;

struct _ZifDb
{
	GObject			 parent;
	ZifDbPrivate		*priv;
};

struct _ZifDbClass
{
	GObjectClass		 parent_class;
};

typedef enum {
	ZIF_DB_ERROR_FAILED,
	ZIF_DB_ERROR_LAST
} ZifDbError;

GQuark		 zif_db_error_quark		(void);
GType		 zif_db_get_type		(void);
ZifDb		*zif_db_new			(void);

gboolean	 zif_db_set_root		(ZifDb		*db,
						 const gchar	*root,
						 GError		**error);
GPtrArray	*zif_db_get_packages		(ZifDb		*db,
						 GError		**error);

gchar		*zif_db_get_string		(ZifDb		*db,
						 ZifPackage	*package,
						 const gchar	*key,
						 GError		**error);
GPtrArray	*zif_db_get_keys		(ZifDb		*db,
						 ZifPackage	*package,
						 GError		**error);
gboolean	 zif_db_set_string		(ZifDb		*db,
						 ZifPackage	*package,
						 const gchar	*key,
						 const gchar	*value,
						 GError		**error);
gboolean	 zif_db_remove			(ZifDb		*db,
						 ZifPackage	*package,
						 const gchar	*key,
						 GError		**error);
gboolean	 zif_db_remove_all		(ZifDb		*db,
						 ZifPackage	*package,
						 GError		**error);

G_END_DECLS

#endif /* __ZIF_DB_H */
