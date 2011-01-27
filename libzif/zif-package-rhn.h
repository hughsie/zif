/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_PACKAGE_RHN_H
#define __ZIF_PACKAGE_RHN_H

#include <glib-object.h>

#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_PACKAGE_RHN		(zif_package_rhn_get_type ())
#define ZIF_PACKAGE_RHN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_PACKAGE_RHN, ZifPackageRhn))
#define ZIF_PACKAGE_RHN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_PACKAGE_RHN, ZifPackageRhnClass))
#define ZIF_IS_PACKAGE_RHN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_PACKAGE_RHN))
#define ZIF_IS_PACKAGE_RHN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_PACKAGE_RHN))
#define ZIF_PACKAGE_RHN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_PACKAGE_RHN, ZifPackageRhnClass))

typedef struct _ZifPackageRhn		ZifPackageRhn;
typedef struct _ZifPackageRhnPrivate	ZifPackageRhnPrivate;
typedef struct _ZifPackageRhnClass	ZifPackageRhnClass;

struct _ZifPackageRhn
{
	ZifPackage		 parent;
	ZifPackageRhnPrivate	*priv;
};

struct _ZifPackageRhnClass
{
	ZifPackageClass		 parent_class;
};

GType		 zif_package_rhn_get_type	(void);
ZifPackage	*zif_package_rhn_new		(void);
void		 zif_package_rhn_set_id		(ZifPackageRhn		*pkg,
						 guint			 id);
void		 zif_package_rhn_set_server	(ZifPackageRhn		*pkg,
						 const gchar		*server);
void		 zif_package_rhn_set_session_key (ZifPackageRhn		*pkg,
						 const gchar		*session_key);
guint		 zif_package_rhn_get_id		(ZifPackageRhn		*pkg);

G_END_DECLS

#endif /* __ZIF_PACKAGE_RHN_H */

