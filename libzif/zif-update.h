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

#ifndef __ZIF_UPDATE_H
#define __ZIF_UPDATE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "zif-update-info.h"

G_BEGIN_DECLS

#define ZIF_TYPE_UPDATE		(zif_update_get_type ())
#define ZIF_UPDATE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_UPDATE, ZifUpdate))
#define ZIF_UPDATE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_UPDATE, ZifUpdateClass))
#define ZIF_IS_UPDATE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_UPDATE))
#define ZIF_IS_UPDATE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_UPDATE))
#define ZIF_UPDATE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_UPDATE, ZifUpdateClass))
#define ZIF_UPDATE_ERROR	(zif_update_error_quark ())

typedef struct _ZifUpdate	 ZifUpdate;
typedef struct _ZifUpdatePrivate ZifUpdatePrivate;
typedef struct _ZifUpdateClass	 ZifUpdateClass;

typedef enum {
	ZIF_UPDATE_STATE_STABLE,
	ZIF_UPDATE_STATE_TESTING,
	ZIF_UPDATE_STATE_UNKNOWN
} ZifUpdateState;

typedef enum {
	ZIF_UPDATE_KIND_BUGFIX,
	ZIF_UPDATE_KIND_SECURITY,
	ZIF_UPDATE_KIND_ENHANCEMENT,
	ZIF_UPDATE_KIND_NEWPACKAGE,
	ZIF_UPDATE_KIND_UNKNOWN
} ZifUpdateKind;

#include "zif-package.h"
#include "zif-changeset.h"

struct _ZifUpdate
{
	GObject			 parent;
	ZifUpdatePrivate	*priv;
};

struct _ZifUpdateClass
{
	GObjectClass		 parent_class;
	/* Padding for future expansion */
	void (*_zif_reserved1) (void);
	void (*_zif_reserved2) (void);
	void (*_zif_reserved3) (void);
	void (*_zif_reserved4) (void);
};

GType			 zif_update_get_type		(void);
ZifUpdate		*zif_update_new			(void);

ZifUpdateState		 zif_update_state_from_string	(const gchar		*state);
ZifUpdateKind		 zif_update_kind_from_string	(const gchar		*kind);
const gchar		*zif_update_state_to_string	(ZifUpdateState		 state);
const gchar		*zif_update_kind_to_string	(ZifUpdateKind		 kind);

ZifUpdateState		 zif_update_get_state		(ZifUpdate		*update);
ZifUpdateKind		 zif_update_get_kind		(ZifUpdate		*update);
const gchar		*zif_update_get_id		(ZifUpdate		*update);
const gchar		*zif_update_get_title		(ZifUpdate		*update);
const gchar		*zif_update_get_description	(ZifUpdate		*update);
const gchar		*zif_update_get_issued		(ZifUpdate		*update);
const gchar		*zif_update_get_source		(ZifUpdate		*update);
gboolean		 zif_update_get_reboot		(ZifUpdate		*update);
GPtrArray		*zif_update_get_update_infos	(ZifUpdate		*update);
GPtrArray		*zif_update_get_packages	(ZifUpdate		*update);
GPtrArray		*zif_update_get_changelog	(ZifUpdate		*update);

G_END_DECLS

#endif /* __ZIF_UPDATE_H */

