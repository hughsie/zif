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

#ifndef __ZIF_STORE_REMOTE_H
#define __ZIF_STORE_REMOTE_H

#include <glib-object.h>
#include <packagekit-glib/packagekit.h>

#include "zif-store.h"
#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_STORE_REMOTE		(zif_store_remote_get_type ())
#define ZIF_STORE_REMOTE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_STORE_REMOTE, ZifStoreRemote))
#define ZIF_STORE_REMOTE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_STORE_REMOTE, ZifStoreRemoteClass))
#define ZIF_IS_STORE_REMOTE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_STORE_REMOTE))
#define ZIF_IS_STORE_REMOTE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_STORE_REMOTE))
#define ZIF_STORE_REMOTE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_STORE_REMOTE, ZifStoreRemoteClass))

typedef struct _ZifStoreRemote		ZifStoreRemote;
typedef struct _ZifStoreRemotePrivate	ZifStoreRemotePrivate;
typedef struct _ZifStoreRemoteClass	ZifStoreRemoteClass;

struct _ZifStoreRemote
{
	ZifStore		 parent;
	ZifStoreRemotePrivate	*priv;
};

struct _ZifStoreRemoteClass
{
	ZifStoreClass		 parent_class;
};

GType		 zif_store_remote_get_type		(void);
ZifStoreRemote	*zif_store_remote_new			(void);
gboolean	 zif_store_remote_set_from_file		(ZifStoreRemote		*store,
							 const gchar		*filename,
							 const gchar		*id,
							 GError			**error);
gboolean	 zif_store_remote_is_devel		(ZifStoreRemote		*store,
							 GError			**error);
const gchar	*zif_store_remote_get_name		(ZifStoreRemote		*store,
							 GError			**error);
gboolean	 zif_store_remote_get_enabled		(ZifStoreRemote		*store,
							 GError			**error);
gboolean	 zif_store_remote_set_enabled		(ZifStoreRemote		*store,
							 gboolean		 enabled,
							 GError			**error);
gboolean	 zif_store_remote_download		(ZifStoreRemote		*store,
							 const gchar		*filename,
							 const gchar		*directory,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
gboolean	 zif_store_remote_refresh_cache		(ZifStoreRemote		*store,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_STORE_REMOTE_H */

