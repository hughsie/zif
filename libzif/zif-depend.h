/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_DEPEND_H
#define __ZIF_DEPEND_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define ZIF_TYPE_DEPEND		(zif_depend_get_type ())
#define ZIF_DEPEND(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_DEPEND, ZifDepend))
#define ZIF_DEPEND_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_DEPEND, ZifDependClass))
#define ZIF_IS_DEPEND(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_DEPEND))
#define ZIF_IS_DEPEND_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_DEPEND))
#define ZIF_DEPEND_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_DEPEND, ZifDependClass))
#define ZIF_DEPEND_ERROR	(zif_depend_error_quark ())

typedef struct _ZifDepend	 ZifDepend;
typedef struct _ZifDependPrivate ZifDependPrivate;
typedef struct _ZifDependClass	 ZifDependClass;

typedef enum {
	ZIF_DEPEND_FLAG_ANY,
	ZIF_DEPEND_FLAG_LESS,
	ZIF_DEPEND_FLAG_GREATER,
	ZIF_DEPEND_FLAG_EQUAL,
	ZIF_DEPEND_FLAG_UNKNOWN
} ZifDependFlag;

#include "zif-package.h"
#include "zif-changeset.h"

struct _ZifDepend
{
	GObject			 parent;
	ZifDependPrivate	*priv;
};

struct _ZifDependClass
{
	GObjectClass		 parent_class;
};

GType			 zif_depend_get_type		(void);
ZifDepend		*zif_depend_new			(void);

/* utility functions */
gchar			*zif_depend_to_string		(ZifDepend		*depend);
const gchar		*zif_depend_flag_to_string	(ZifDependFlag		 flag);

/* public getters */
ZifDependFlag		 zif_depend_get_flag		(ZifDepend		*depend);
const gchar		*zif_depend_get_name		(ZifDepend		*depend);
const gchar		*zif_depend_get_version		(ZifDepend		*depend);

/* internal setters */
void			 zif_depend_set_flag		(ZifDepend		*depend,
							 ZifDependFlag		 flag);
void			 zif_depend_set_name		(ZifDepend		*depend,
							 const gchar		*name);
void			 zif_depend_set_version		(ZifDepend		*depend,
							 const gchar		*version);

G_END_DECLS

#endif /* __ZIF_DEPEND_H */

