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

#ifndef __ZIF_PACKAGE_H
#define __ZIF_PACKAGE_H

#include <glib-object.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

#define ZIF_TYPE_PACKAGE		(zif_package_get_type ())
#define ZIF_PACKAGE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_PACKAGE, ZifPackage))
#define ZIF_PACKAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_PACKAGE, ZifPackageClass))
#define ZIF_IS_PACKAGE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_PACKAGE))
#define ZIF_IS_PACKAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_PACKAGE))
#define ZIF_PACKAGE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_PACKAGE, ZifPackageClass))

typedef struct ZifPackagePrivate ZifPackagePrivate;

typedef struct
{
	GObject			 parent;
	ZifPackagePrivate	*priv;
} ZifPackage;

typedef struct
{
	GObjectClass	parent_class;
} ZifPackageClass;

GType			 zif_package_get_type		(void) G_GNUC_CONST;
ZifPackage		*zif_package_new		(void);

/* public getters */
const PkPackageId	*zif_package_get_id		(ZifPackage	*package);
const gchar		*zif_package_get_summary	(ZifPackage	*package,
							 GError		**error);
const gchar		*zif_package_get_description	(ZifPackage	*package,
							 GError		**error);
const gchar		*zif_package_get_license	(ZifPackage	*package,
							 GError		**error);
const gchar		*zif_package_get_url		(ZifPackage	*package,
							 GError		**error);
const gchar		*zif_package_get_filename	(ZifPackage	*package,
							 GError		**error);
const gchar		*zif_package_get_category	(ZifPackage	*package,
							 GError		**error);
PkGroupEnum		 zif_package_get_group		(ZifPackage	*package,
							 GError		**error);
guint64			 zif_package_get_size		(ZifPackage	*package,
							 GError		**error);
gchar			**zif_package_get_files		(ZifPackage	*package,
							 GError		**error);
gchar			**zif_package_get_requires	(ZifPackage	*package,
							 GError		**error);
gchar			**zif_package_get_provides	(ZifPackage	*package,
							 GError		**error);

/* internal setters: TODO, in seporate -internal header file */
gboolean		 zif_package_set_id		(ZifPackage	*package,
							 const PkPackageId *id);
gboolean		 zif_package_set_summary	(ZifPackage	*package,
							 const gchar	*summary);
gboolean		 zif_package_set_description	(ZifPackage	*package,
							 const gchar	*description);
gboolean		 zif_package_set_license	(ZifPackage	*package,
							 const gchar	*license);
gboolean		 zif_package_set_url		(ZifPackage	*package,
							 const gchar	*url);
gboolean		 zif_package_set_location_href	(ZifPackage	*package,
							 const gchar	*filename);
gboolean		 zif_package_set_category	(ZifPackage	*package,
							 const gchar	*category);
gboolean		 zif_package_set_group		(ZifPackage	*package,
							 PkGroupEnum	 group);
gboolean		 zif_package_set_size		(ZifPackage	*package,
							 guint64	 size);
gboolean		 zif_package_set_files		(ZifPackage	*package,
							 gchar		**files);
gboolean		 zif_package_set_requires	(ZifPackage	*package,
							 gchar		**requires);
gboolean		 zif_package_set_provides	(ZifPackage	*package,
							 gchar		**provides);
/* actions */
gboolean		 zif_package_download		(ZifPackage	*package,
							 const gchar	*directory,
							 GError		**error);
gboolean		 zif_package_set_from_header	(ZifPackage	*package,
							 Header		 header,
							 GError		**error);
gboolean		 zif_package_set_from_repo	(ZifPackage	*package,
							 guint		 length,
							 gchar		**type,
							 gchar		**data,
							 const gchar	*repo_id,
							 GError		**error);
const gchar		*zif_package_get_package_id	(ZifPackage	*package);
void			 zif_package_print		(ZifPackage	*package);
gboolean		 zif_package_is_devel		(ZifPackage	*package);
gboolean		 zif_package_is_gui		(ZifPackage	*package);
gboolean		 zif_package_is_installed	(ZifPackage	*package);
gboolean		 zif_package_is_free		(ZifPackage	*package);

G_END_DECLS

#endif /* __ZIF_PACKAGE_H */

