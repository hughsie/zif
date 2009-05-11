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

#ifndef __DUM_CONFIG_H
#define __DUM_CONFIG_H

#include <glib-object.h>

G_BEGIN_DECLS

#define DUM_TYPE_CONFIG		(dum_config_get_type ())
#define DUM_CONFIG(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_CONFIG, DumConfig))
#define DUM_CONFIG_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_CONFIG, DumConfigClass))
#define DUM_IS_CONFIG(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_CONFIG))
#define DUM_IS_CONFIG_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_CONFIG))
#define DUM_CONFIG_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_CONFIG, DumConfigClass))

typedef struct DumConfigPrivate DumConfigPrivate;

typedef struct
{
	GObject		      parent;
	DumConfigPrivate     *priv;
} DumConfig;

typedef struct
{
	GObjectClass	parent_class;
} DumConfigClass;

GType		 dum_config_get_type		(void) G_GNUC_CONST;
DumConfig	*dum_config_new			(void);
gboolean	 dum_config_set_filename	(DumConfig	*config,
						 const gchar	*filename,
						 GError		**error);
gchar		*dum_config_get_string		(DumConfig	*config,
						 const gchar	*key,
						 GError		**error);
gboolean	 dum_config_get_boolean		(DumConfig	*config,
						 const gchar	*key,
						 GError		**error);

G_END_DECLS

#endif /* __DUM_CONFIG_H */
