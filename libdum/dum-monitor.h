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

#ifndef __DUM_MONITOR_H
#define __DUM_MONITOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define DUM_TYPE_MONITOR		(dum_monitor_get_type ())
#define DUM_MONITOR(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_MONITOR, DumMonitor))
#define DUM_MONITOR_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_MONITOR, DumMonitorClass))
#define DUM_IS_MONITOR(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_MONITOR))
#define DUM_IS_MONITOR_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_MONITOR))
#define DUM_MONITOR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_MONITOR, DumMonitorClass))

typedef struct DumMonitorPrivate DumMonitorPrivate;

typedef struct
{
	GObject			 parent;
	DumMonitorPrivate	*priv;
} DumMonitor;

typedef struct
{
	GObjectClass	parent_class;
} DumMonitorClass;

GType		 dum_monitor_get_type		(void) G_GNUC_CONST;
DumMonitor	*dum_monitor_new		(void);
gboolean	 dum_monitor_add_watch		(DumMonitor	*monitor,
						 const gchar	*filename,
						 GError		**error);

G_END_DECLS

#endif /* __DUM_MONITOR_H */

