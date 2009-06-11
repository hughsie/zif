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

#if !defined (__DUM_H_INSIDE__) && !defined (DUM_COMPILATION)
#error "Only <dum.h> can be included directly."
#endif

#ifndef __DUM_PACKAGE_H
#define __DUM_PACKAGE_H

#include <glib-object.h>
#include <packagekit-glib/packagekit.h>

#include "dum-string.h"
#include "dum-string-array.h"
#include "dum-depend-array.h"

G_BEGIN_DECLS

#define DUM_TYPE_PACKAGE		(dum_package_get_type ())
#define DUM_PACKAGE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_PACKAGE, DumPackage))
#define DUM_PACKAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_PACKAGE, DumPackageClass))
#define DUM_IS_PACKAGE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_PACKAGE))
#define DUM_IS_PACKAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_PACKAGE))
#define DUM_PACKAGE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_PACKAGE, DumPackageClass))

typedef struct DumPackagePrivate DumPackagePrivate;

typedef struct
{
	GObject			 parent;
	DumPackagePrivate	*priv;
} DumPackage;

typedef struct
{
	GObjectClass	parent_class;
} DumPackageClass;

GType			 dum_package_get_type		(void) G_GNUC_CONST;
DumPackage		*dum_package_new		(void);

/* public getters */
const PkPackageId	*dum_package_get_id		(DumPackage	*package);
DumString		*dum_package_get_summary	(DumPackage	*package,
							 GError		**error);
DumString		*dum_package_get_description	(DumPackage	*package,
							 GError		**error);
DumString		*dum_package_get_license	(DumPackage	*package,
							 GError		**error);
DumString		*dum_package_get_url		(DumPackage	*package,
							 GError		**error);
DumString		*dum_package_get_filename	(DumPackage	*package,
							 GError		**error);
DumString		*dum_package_get_category	(DumPackage	*package,
							 GError		**error);
PkGroupEnum		 dum_package_get_group		(DumPackage	*package,
							 GError		**error);
guint64			 dum_package_get_size		(DumPackage	*package,
							 GError		**error);
DumStringArray		*dum_package_get_files		(DumPackage	*package,
							 GError		**error);
DumDependArray		*dum_package_get_requires	(DumPackage	*package,
							 GError		**error);
DumDependArray		*dum_package_get_provides	(DumPackage	*package,
							 GError		**error);

/* internal setters: TODO, in seporate -internal header file */
gboolean		 dum_package_set_installed	(DumPackage	*package,
							 gboolean	 installed);
gboolean		 dum_package_set_id		(DumPackage	*package,
							 const PkPackageId *id);
gboolean		 dum_package_set_summary	(DumPackage	*package,
							 DumString	*summary);
gboolean		 dum_package_set_description	(DumPackage	*package,
							 DumString	*description);
gboolean		 dum_package_set_license	(DumPackage	*package,
							 DumString	*license);
gboolean		 dum_package_set_url		(DumPackage	*package,
							 DumString	*url);
gboolean		 dum_package_set_location_href	(DumPackage	*package,
							 DumString	*filename);
gboolean		 dum_package_set_category	(DumPackage	*package,
							 DumString	*category);
gboolean		 dum_package_set_group		(DumPackage	*package,
							 PkGroupEnum	 group);
gboolean		 dum_package_set_size		(DumPackage	*package,
							 guint64	 size);
gboolean		 dum_package_set_files		(DumPackage	*package,
							 DumStringArray	*files);
gboolean		 dum_package_set_requires	(DumPackage	*package,
							 DumDependArray	*requires);
gboolean		 dum_package_set_provides	(DumPackage	*package,
							 DumDependArray	*provides);
/* actions */
gboolean		 dum_package_download		(DumPackage	*package,
							 const gchar	*directory,
							 GError		**error);
const gchar		*dum_package_get_package_id	(DumPackage	*package);
void			 dum_package_print		(DumPackage	*package);
gboolean		 dum_package_is_devel		(DumPackage	*package);
gboolean		 dum_package_is_gui		(DumPackage	*package);
gboolean		 dum_package_is_installed	(DumPackage	*package);
gboolean		 dum_package_is_free		(DumPackage	*package);
gint			 dum_package_compare		(DumPackage	*a,
							 DumPackage	*b);

G_END_DECLS

#endif /* __DUM_PACKAGE_H */

