/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_SACK_LOCAL_H
#define __ZIF_SACK_LOCAL_H

#include <glib-object.h>

#include "zif-sack.h"

G_BEGIN_DECLS

#define ZIF_TYPE_SACK_LOCAL		(zif_sack_local_get_type ())
#define ZIF_SACK_LOCAL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_SACK_LOCAL, ZifSackLocal))
#define ZIF_SACK_LOCAL_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_SACK_LOCAL, ZifSackLocalClass))
#define ZIF_IS_SACK_LOCAL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_SACK_LOCAL))
#define ZIF_IS_SACK_LOCAL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_SACK_LOCAL))
#define ZIF_SACK_LOCAL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_SACK_LOCAL, ZifSackLocalClass))

typedef struct ZifSackLocalPrivate ZifSackLocalPrivate;

typedef struct
{
	ZifSack			 parent;
	ZifSackLocalPrivate	*priv;
} ZifSackLocal;

typedef struct
{
	GObjectClass		 parent_class;
} ZifSackLocalClass;

GType		 zif_sack_local_get_type		(void) G_GNUC_CONST;
ZifSackLocal	*zif_sack_local_new			(void);

G_END_DECLS

#endif /* __ZIF_SACK_LOCAL_H */

