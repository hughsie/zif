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

#ifndef __ZIF_UPGRADE_H
#define __ZIF_UPGRADE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ZIF_TYPE_UPGRADE		(zif_upgrade_get_type ())
#define ZIF_UPGRADE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_UPGRADE, ZifUpgrade))
#define ZIF_UPGRADE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_UPGRADE, ZifUpgradeClass))
#define ZIF_IS_UPGRADE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_UPGRADE))
#define ZIF_IS_UPGRADE_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_UPGRADE))
#define ZIF_UPGRADE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_UPGRADE, ZifUpgradeClass))
#define ZIF_UPGRADE_ERROR		(zif_upgrade_error_quark ())

typedef struct _ZifUpgrade	 ZifUpgrade;
typedef struct _ZifUpgradePrivate ZifUpgradePrivate;
typedef struct _ZifUpgradeClass	 ZifUpgradeClass;

#include "zif-package.h"

struct _ZifUpgrade
{
	GObject			 parent;
	ZifUpgradePrivate	*priv;
};

struct _ZifUpgradeClass
{
	GObjectClass		 parent_class;
};

GType			 zif_upgrade_get_type			(void);
ZifUpgrade		*zif_upgrade_new			(void);

/* public getters */
const gchar		*zif_upgrade_get_id			(ZifUpgrade		*upgrade);
gboolean		 zif_upgrade_get_stable			(ZifUpgrade		*upgrade);
gboolean		 zif_upgrade_get_enabled		(ZifUpgrade		*upgrade);
guint			 zif_upgrade_get_version		(ZifUpgrade		*upgrade);
const gchar		*zif_upgrade_get_baseurl		(ZifUpgrade		*upgrade);
const gchar		*zif_upgrade_get_mirrorlist		(ZifUpgrade		*upgrade);
const gchar		*zif_upgrade_get_install_mirrorlist	(ZifUpgrade		*upgrade);

/* internal setters */
void			 zif_upgrade_set_id			(ZifUpgrade		*upgrade,
								 const gchar		*id);
void			 zif_upgrade_set_stable			(ZifUpgrade		*upgrade,
								 gboolean		 stable);
void			 zif_upgrade_set_enabled		(ZifUpgrade		*upgrade,
								 gboolean		 enabled);
void			 zif_upgrade_set_version		(ZifUpgrade		*upgrade,
								 guint			 version);
void			 zif_upgrade_set_baseurl		(ZifUpgrade		*upgrade,
								 const gchar		*baseurl);
void			 zif_upgrade_set_mirrorlist		(ZifUpgrade		*upgrade,
								 const gchar		*mirrorlist);
void			 zif_upgrade_set_install_mirrorlist	(ZifUpgrade		*upgrade,
								 const gchar		*install_mirrorlist);

G_END_DECLS

#endif /* __ZIF_UPGRADE_H */

