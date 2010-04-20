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

#ifndef __ZIF_LEGAL_H
#define __ZIF_LEGAL_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ZIF_TYPE_LEGAL		(zif_legal_get_type ())
#define ZIF_LEGAL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_LEGAL, ZifLegal))
#define ZIF_LEGAL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_LEGAL, ZifLegalClass))
#define ZIF_IS_LEGAL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_LEGAL))
#define ZIF_IS_LEGAL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_LEGAL))
#define ZIF_LEGAL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_LEGAL, ZifLegalClass))
#define ZIF_LEGAL_ERROR		(zif_legal_error_quark ())

typedef struct _ZifLegal		ZifLegal;
typedef struct _ZifLegalPrivate		ZifLegalPrivate;
typedef struct _ZifLegalClass		ZifLegalClass;

struct _ZifLegal
{
	GObject			 parent;
	ZifLegalPrivate		*priv;
};

struct _ZifLegalClass
{
	GObjectClass		 parent_class;
};

typedef enum {
	ZIF_LEGAL_ERROR_FAILED,
	ZIF_LEGAL_ERROR_LAST
} ZifLegalError;

GQuark		 zif_legal_error_quark		(void);
GType		 zif_legal_get_type		(void);
ZifLegal	*zif_legal_new			(void);
void		 zif_legal_set_filename		(ZifLegal	*legal,
						 const gchar	*filename);
gboolean	 zif_legal_is_free		(ZifLegal	*legal,
						 const gchar	*string,
						 gboolean	*is_free,
						 GError		**error);

G_END_DECLS

#endif /* __ZIF_LEGAL_H */
