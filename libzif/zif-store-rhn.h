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

#ifndef __ZIF_STORE_RHN_H
#define __ZIF_STORE_RHN_H

#include <glib-object.h>

#include "zif-store.h"
#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_STORE_RHN		(zif_store_rhn_get_type ())
#define ZIF_STORE_RHN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_STORE_RHN, ZifStoreRhn))
#define ZIF_STORE_RHN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_STORE_RHN, ZifStoreRhnClass))
#define ZIF_IS_STORE_RHN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_STORE_RHN))
#define ZIF_IS_STORE_RHN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_STORE_RHN))
#define ZIF_STORE_RHN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_STORE_RHN, ZifStoreRhnClass))

typedef struct _ZifStoreRhn		ZifStoreRhn;
typedef struct _ZifStoreRhnPrivate	ZifStoreRhnPrivate;
typedef struct _ZifStoreRhnClass	ZifStoreRhnClass;

struct _ZifStoreRhn
{
	ZifStore		 parent;
	ZifStoreRhnPrivate	*priv;
};

struct _ZifStoreRhnClass
{
	ZifStoreClass		 parent_class;
};

GType		 zif_store_rhn_get_type	(void);
ZifStore	*zif_store_rhn_new		(void);
void		 zif_store_rhn_set_server	(ZifStoreRhn		*store,
						 const gchar		*server);
void		 zif_store_rhn_set_channel	(ZifStoreRhn		*store,
						 const gchar		*channel);
gboolean	 zif_store_rhn_login		(ZifStoreRhn		*store,
						 const gchar		*username,
						 const gchar		*password,
						 GError			**error);
gboolean	 zif_store_rhn_logout		(ZifStoreRhn		*store,
						 GError			**error);
gchar		*zif_store_rhn_get_version	(ZifStoreRhn		*store,
						 GError			**error);
const gchar	*zif_store_rhn_get_session_key	(ZifStoreRhn		*store);

G_END_DECLS

#endif /* __ZIF_STORE_RHN_H */

