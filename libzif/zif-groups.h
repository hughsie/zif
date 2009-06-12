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

#ifndef __ZIF_GROUPS_H
#define __ZIF_GROUPS_H

#include <glib-object.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

#define ZIF_TYPE_GROUPS		(zif_groups_get_type ())
#define ZIF_GROUPS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_GROUPS, ZifGroups))
#define ZIF_GROUPS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_GROUPS, ZifGroupsClass))
#define ZIF_IS_GROUPS(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_GROUPS))
#define ZIF_IS_GROUPS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_GROUPS))
#define ZIF_GROUPS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_GROUPS, ZifGroupsClass))

typedef struct ZifGroupsPrivate ZifGroupsPrivate;

typedef struct
{
	GObject		      parent;
	ZifGroupsPrivate     *priv;
} ZifGroups;

typedef struct
{
	GObjectClass	parent_class;
} ZifGroupsClass;

GType		 zif_groups_get_type		(void) G_GNUC_CONST;
ZifGroups	*zif_groups_new			(void);
gboolean	 zif_groups_set_mapping_file	(ZifGroups	*groups,
						 const gchar	*mapping_file,
						 GError		**error);
gboolean	 zif_groups_load		(ZifGroups	*groups,
						 GError		**error);
PkBitfield	 zif_groups_get_groups		(ZifGroups	*groups,
						 GError		**error);
GPtrArray	*zif_groups_get_categories	(ZifGroups	*groups,
						 GError		**error);
PkGroupEnum	 zif_groups_get_group_for_cat	(ZifGroups	*groups,
						 const gchar	*cat,
						 GError		**error);

G_END_DECLS

#endif /* __ZIF_GROUPS_H */
