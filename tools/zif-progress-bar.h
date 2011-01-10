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

#ifndef __ZIF_PROGRESS_BAR_H
#define __ZIF_PROGRESS_BAR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ZIF_TYPE_PROGRESS_BAR		(zif_progress_bar_get_type ())
#define ZIF_PROGRESS_BAR(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_PROGRESS_BAR, ZifProgressBar))
#define ZIF_PROGRESS_BAR_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_PROGRESS_BAR, ZifProgressBarClass))
#define ZIF_IS_PROGRESS_BAR(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_PROGRESS_BAR))
#define ZIF_IS_PROGRESS_BAR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_PROGRESS_BAR))
#define ZIF_PROGRESS_BAR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_PROGRESS_BAR, ZifProgressBarClass))

typedef struct ZifProgressBarPrivate ZifProgressBarPrivate;

typedef struct
{
	GObject			 parent;
	ZifProgressBarPrivate	*priv;
} ZifProgressBar;

typedef struct
{
	GObjectClass		 parent_class;
} ZifProgressBarClass;

GType		 zif_progress_bar_get_type		(void);
ZifProgressBar	*zif_progress_bar_new			(void);
void		 zif_progress_bar_set_size		(ZifProgressBar	*progress_bar,
							 guint		 size);
void		 zif_progress_bar_set_padding		(ZifProgressBar	*progress_bar,
							 guint		 padding);
void		 zif_progress_bar_set_percentage	(ZifProgressBar	*progress_bar,
							 guint		 percentage);
void		 zif_progress_bar_set_action		(ZifProgressBar	*progress_bar,
							 const gchar	*action);
void		 zif_progress_bar_set_detail		(ZifProgressBar	*progress_bar,
							 const gchar	*action);
void		 zif_progress_bar_set_allow_cancel	(ZifProgressBar	*progress_bar,
							 gboolean	 allow_cancel);
void		 zif_progress_bar_set_speed		(ZifProgressBar	*progress_bar,
							 guint64	 speed);
void		 zif_progress_bar_set_on_console	(ZifProgressBar	*progress_bar,
							 gboolean	 on_console);

void		 zif_progress_bar_start			(ZifProgressBar	*progress_bar,
							 const gchar	*action);
void		 zif_progress_bar_end			(ZifProgressBar	*progress_bar);

G_END_DECLS

#endif /* __ZIF_PROGRESS_BAR_H */
