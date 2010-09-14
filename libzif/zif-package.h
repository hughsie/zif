/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_PACKAGE_H
#define __ZIF_PACKAGE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "zif-string.h"
#include "zif-state.h"

G_BEGIN_DECLS

#define ZIF_TYPE_PACKAGE		(zif_package_get_type ())
#define ZIF_PACKAGE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_PACKAGE, ZifPackage))
#define ZIF_PACKAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_PACKAGE, ZifPackageClass))
#define ZIF_IS_PACKAGE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_PACKAGE))
#define ZIF_IS_PACKAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_PACKAGE))
#define ZIF_PACKAGE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_PACKAGE, ZifPackageClass))
#define ZIF_PACKAGE_ERROR		(zif_package_error_quark ())

typedef struct _ZifPackage		ZifPackage;
typedef struct _ZifPackagePrivate	ZifPackagePrivate;
typedef struct _ZifPackageClass		ZifPackageClass;

#include "zif-update.h"

typedef enum {
	ZIF_PACKAGE_ENSURE_TYPE_FILES,
	ZIF_PACKAGE_ENSURE_TYPE_SUMMARY,
	ZIF_PACKAGE_ENSURE_TYPE_LICENCE,
	ZIF_PACKAGE_ENSURE_TYPE_DESCRIPTION,
	ZIF_PACKAGE_ENSURE_TYPE_URL,
	ZIF_PACKAGE_ENSURE_TYPE_SIZE,
	ZIF_PACKAGE_ENSURE_TYPE_GROUP,
	ZIF_PACKAGE_ENSURE_TYPE_CATEGORY,
	ZIF_PACKAGE_ENSURE_TYPE_REQUIRES,
	ZIF_PACKAGE_ENSURE_TYPE_PROVIDES,
	ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS,
	ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES,
	ZIF_PACKAGE_ENSURE_TYPE_LAST
} ZifPackageEnsureType;

struct _ZifPackage
{
	GObject			 parent;
	ZifPackagePrivate	*priv;
};

struct _ZifPackageClass
{
	GObjectClass	parent_class;

	/* vtable */
	gboolean	 (*ensure_data)			(ZifPackage	*package,
							 ZifPackageEnsureType type,
							 ZifState	*state,
							 GError		**error);
};

typedef enum {
	ZIF_PACKAGE_ERROR_FAILED,
	ZIF_PACKAGE_ERROR_LAST
} ZifPackageError;

GType			 zif_package_get_type		(void);
GQuark			 zif_package_error_quark	(void);
ZifPackage		*zif_package_new		(void);

/* public getters */
const gchar		*zif_package_get_id		(ZifPackage	*package);
const gchar		*zif_package_get_name		(ZifPackage	*package);
const gchar		*zif_package_get_version	(ZifPackage	*package);
const gchar		*zif_package_get_arch		(ZifPackage	*package);
const gchar		*zif_package_get_data		(ZifPackage	*package);
const gchar		*zif_package_get_summary	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_description	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_license	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_url		(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_filename	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_category	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_group		(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
guint64			 zif_package_get_size		(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray		*zif_package_get_files		(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray		*zif_package_get_requires	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray		*zif_package_get_provides	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);

/* internal setters */
gboolean		 zif_package_set_id		(ZifPackage	*package,
							 const gchar	*package_id,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
void			 zif_package_set_installed	(ZifPackage	*package,
							 gboolean	 installed);
void			 zif_package_set_summary	(ZifPackage	*package,
							 ZifString	*summary);
void			 zif_package_set_description	(ZifPackage	*package,
							 ZifString	*description);
void			 zif_package_set_license	(ZifPackage	*package,
							 ZifString	*license);
void			 zif_package_set_url		(ZifPackage	*package,
							 ZifString	*url);
void			 zif_package_set_location_href	(ZifPackage	*package,
							 ZifString	*location_href);
void			 zif_package_set_category	(ZifPackage	*package,
							 ZifString	*category);
void			 zif_package_set_group		(ZifPackage	*package,
							 ZifString	*group);
void			 zif_package_set_size		(ZifPackage	*package,
							 guint64	 size);
void			 zif_package_set_files		(ZifPackage	*package,
							 GPtrArray	*files);
void			 zif_package_set_requires	(ZifPackage	*package,
							 GPtrArray	*requires);
void			 zif_package_set_provides	(ZifPackage	*package,
							 GPtrArray	*provides);
/* actions */
gboolean		 zif_package_download		(ZifPackage	*package,
							 const gchar	*directory,
							 ZifState	*state,
							 GError		**error);
ZifUpdate		*zif_package_get_update_detail	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_package_id	(ZifPackage	*package);
void			 zif_package_print		(ZifPackage	*package);
gboolean		 zif_package_is_devel		(ZifPackage	*package);
gboolean		 zif_package_is_gui		(ZifPackage	*package);
gboolean		 zif_package_is_installed	(ZifPackage	*package);
gboolean		 zif_package_is_free		(ZifPackage	*package);
gboolean		 zif_package_is_native		(ZifPackage	*package);
gint			 zif_package_compare		(ZifPackage	*a,
							 ZifPackage	*b);
ZifPackage		*zif_package_array_get_newest	(GPtrArray	*array,
							 GError		**error);
gboolean		 zif_package_array_filter_newest (GPtrArray	*packages);
const gchar		*zif_package_ensure_type_to_string (ZifPackageEnsureType type);

G_END_DECLS

#endif /* __ZIF_PACKAGE_H */

