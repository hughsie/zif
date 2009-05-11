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

#ifndef __DUM_SACK_H
#define __DUM_SACK_H

#include <glib-object.h>

#include "dum-store.h"
#include "dum-package.h"

G_BEGIN_DECLS

#define DUM_TYPE_SACK		(dum_sack_get_type ())
#define DUM_SACK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_SACK, DumSack))
#define DUM_SACK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_SACK, DumSackClass))
#define DUM_IS_SACK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_SACK))
#define DUM_IS_SACK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_SACK))
#define DUM_SACK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_SACK, DumSackClass))

typedef struct DumSackPrivate DumSackPrivate;

typedef struct
{
	GObject			 parent;
	DumSackPrivate	*priv;
} DumSack;

typedef struct
{
	GObjectClass	parent_class;
} DumSackClass;

GType		 dum_sack_get_type		(void) G_GNUC_CONST;
DumSack		*dum_sack_new			(void);

/* stores */
gboolean	 dum_sack_add_store		(DumSack		*sack,
						 DumStore		*store);
gboolean	 dum_sack_add_stores		(DumSack		*sack,
						 GPtrArray		*stores);

/* methods */
GPtrArray	*dum_sack_resolve		(DumSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_sack_search_name		(DumSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_sack_search_details	(DumSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_sack_search_group		(DumSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_sack_search_file		(DumSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_sack_what_provides		(DumSack		*sack,
						 const gchar		*search,
						 GError			**error);
GPtrArray	*dum_sack_get_packages		(DumSack		*sack,
						 GError			**error);
DumPackage	*dum_sack_find_package		(DumSack		*sack,
						 const PkPackageId	*id,
						 GError			**error);

G_END_DECLS

#endif /* __DUM_SACK_H */

