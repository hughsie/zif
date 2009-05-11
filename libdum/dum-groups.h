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

#ifndef __DUM_GROUPS_H
#define __DUM_GROUPS_H

#include <glib-object.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

#define DUM_TYPE_GROUPS		(dum_groups_get_type ())
#define DUM_GROUPS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_GROUPS, DumGroups))
#define DUM_GROUPS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_GROUPS, DumGroupsClass))
#define DUM_IS_GROUPS(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_GROUPS))
#define DUM_IS_GROUPS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_GROUPS))
#define DUM_GROUPS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_GROUPS, DumGroupsClass))

typedef struct DumGroupsPrivate DumGroupsPrivate;

typedef struct
{
	GObject		      parent;
	DumGroupsPrivate     *priv;
} DumGroups;

typedef struct
{
	GObjectClass	parent_class;
} DumGroupsClass;

GType		 dum_groups_get_type		(void) G_GNUC_CONST;
DumGroups	*dum_groups_new			(void);
gboolean	 dum_groups_set_mapping_file	(DumGroups	*groups,
						 const gchar	*mapping_file,
						 GError		**error);
gboolean	 dum_groups_load		(DumGroups	*groups,
						 GError		**error);
PkBitfield	 dum_groups_get_groups		(DumGroups	*groups,
						 GError		**error);
GPtrArray	*dum_groups_get_categories	(DumGroups	*groups,
						 GError		**error);
PkGroupEnum	 dum_groups_get_group_for_cat	(DumGroups	*groups,
						 const gchar	*cat,
						 GError		**error);

G_END_DECLS

#endif /* __DUM_GROUPS_H */
