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

#ifndef __DUM_STORE_REMOTE_H
#define __DUM_STORE_REMOTE_H

#include <glib-object.h>
#include <packagekit-glib/packagekit.h>

#include "dum-store.h"
#include "dum-package.h"

G_BEGIN_DECLS

#define DUM_TYPE_STORE_REMOTE		(dum_store_remote_get_type ())
#define DUM_STORE_REMOTE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_STORE_REMOTE, DumStoreRemote))
#define DUM_STORE_REMOTE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_STORE_REMOTE, DumStoreRemoteClass))
#define DUM_IS_STORE_REMOTE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_STORE_REMOTE))
#define DUM_IS_STORE_REMOTE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_STORE_REMOTE))
#define DUM_STORE_REMOTE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_STORE_REMOTE, DumStoreRemoteClass))

typedef struct DumStoreRemotePrivate DumStoreRemotePrivate;

typedef struct
{
	DumStore		 parent;
	DumStoreRemotePrivate	*priv;
} DumStoreRemote;

typedef struct
{
	DumStoreClass		 parent_class;
} DumStoreRemoteClass;

GType		 dum_store_remote_get_type		(void) G_GNUC_CONST;
DumStoreRemote	*dum_store_remote_new			(void);
gboolean	 dum_store_remote_set_from_file		(DumStoreRemote		*store,
							 const gchar		*filename,
							 const gchar		*id,
							 GError			**error);
gboolean	 dum_store_remote_is_devel		(DumStoreRemote		*store,
							 GError			**error);
const gchar	*dum_store_remote_get_name		(DumStoreRemote		*store,
							 GError			**error);
gboolean	 dum_store_remote_get_enabled		(DumStoreRemote		*store,
							 GError			**error);
gboolean	 dum_store_remote_set_enabled		(DumStoreRemote		*store,
							 gboolean		 enabled,
							 GError			**error);
gboolean	 dum_store_remote_download		(DumStoreRemote		*store,
							 const gchar		*filename,
							 const gchar		*directory,
							 GError			**error);
gboolean	 dum_store_remote_clean			(DumStoreRemote		*store,
							 GError			**error);
GPtrArray	*dum_store_remote_get_updates		(DumStoreRemote		*store,
							 GError			**error);

G_END_DECLS

#endif /* __DUM_STORE_REMOTE_H */

