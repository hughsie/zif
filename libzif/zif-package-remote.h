/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_PACKAGE_REMOTE_H
#define __ZIF_PACKAGE_REMOTE_H

#include <glib-object.h>

#include "zif-delta.h"
#include "zif-package.h"
#include "zif-store-remote.h"

G_BEGIN_DECLS

#define ZIF_TYPE_PACKAGE_REMOTE		(zif_package_remote_get_type ())
#define ZIF_PACKAGE_REMOTE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_PACKAGE_REMOTE, ZifPackageRemote))
#define ZIF_PACKAGE_REMOTE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_PACKAGE_REMOTE, ZifPackageRemoteClass))
#define ZIF_IS_PACKAGE_REMOTE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_PACKAGE_REMOTE))
#define ZIF_IS_PACKAGE_REMOTE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_PACKAGE_REMOTE))
#define ZIF_PACKAGE_REMOTE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_PACKAGE_REMOTE, ZifPackageRemoteClass))

typedef struct _ZifPackageRemote	ZifPackageRemote;
typedef struct _ZifPackageRemotePrivate	ZifPackageRemotePrivate;
typedef struct _ZifPackageRemoteClass	ZifPackageRemoteClass;

struct _ZifPackageRemote
{
	ZifPackage		 parent;
	ZifPackageRemotePrivate	*priv;
};

struct _ZifPackageRemoteClass
{
	ZifPackageClass		 parent_class;
	/* Padding for future expansion */
	void (*_zif_reserved1) (void);
	void (*_zif_reserved2) (void);
	void (*_zif_reserved3) (void);
	void (*_zif_reserved4) (void);
};

GType			 zif_package_remote_get_type		(void);
ZifPackage		*zif_package_remote_new			(void);
gboolean		 zif_package_remote_set_from_repo	(ZifPackageRemote *pkg,
								 guint		 length,
								 gchar		**type,
								 gchar		**data,
								 const gchar	*repo_id,
								 GError		**error)
								 G_GNUC_WARN_UNUSED_RESULT;
void			 zif_package_remote_set_store_remote	(ZifPackageRemote *pkg,
								 ZifStoreRemote	*store);
ZifStoreRemote		*zif_package_remote_get_store_remote	(ZifPackageRemote *pkg);
void			 zif_package_remote_set_installed	(ZifPackageRemote *pkg,
								 ZifPackage	*installed);
ZifPackage		*zif_package_remote_get_installed	(ZifPackageRemote *pkg);
gboolean		 zif_package_remote_download		(ZifPackageRemote *pkg,
								 const gchar	*directory,
								 ZifState	*state,
								 GError		**error);
ZifDelta		*zif_package_remote_download_delta	(ZifPackageRemote *pkg,
								 const gchar *directory,
								 ZifState *state,
								 GError **error);
gboolean		 zif_package_remote_rebuild_delta	(ZifPackageRemote *pkg,
								 ZifDelta *delta,
								 const gchar *directory,
								 ZifState *state,
								 GError **error);
ZifUpdate		*zif_package_remote_get_update_detail	(ZifPackageRemote *package,
								 ZifState	*state,
								 GError		**error);
ZifDelta		*zif_package_remote_get_delta		(ZifPackageRemote *package,
								 ZifState	*state,
								 GError		**error);

G_END_DECLS

#endif /* __ZIF_PACKAGE_REMOTE_H */

