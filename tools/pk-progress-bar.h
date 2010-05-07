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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __PK_PROGRESS_BAR_H
#define __PK_PROGRESS_BAR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_PROGRESS_BAR		(pk_progress_bar_get_type ())
#define PK_PROGRESS_BAR(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PROGRESS_BAR, PkProgressBar))
#define PK_PROGRESS_BAR_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PROGRESS_BAR, PkProgressBarClass))
#define PK_IS_PROGRESS_BAR(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PROGRESS_BAR))
#define PK_IS_PROGRESS_BAR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PROGRESS_BAR))
#define PK_PROGRESS_BAR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PROGRESS_BAR, PkProgressBarClass))

typedef struct PkProgressBarPrivate PkProgressBarPrivate;

typedef struct
{
	GObject			 parent;
	PkProgressBarPrivate	*priv;
} PkProgressBar;

typedef struct
{
	GObjectClass		 parent_class;
} PkProgressBarClass;

GType		 pk_progress_bar_get_type		(void);
PkProgressBar	*pk_progress_bar_new			(void);
gboolean	 pk_progress_bar_set_size		(PkProgressBar	*progress_bar,
							 guint		 size);
gboolean	 pk_progress_bar_set_padding		(PkProgressBar	*progress_bar,
							 guint		 padding);
gboolean	 pk_progress_bar_set_percentage		(PkProgressBar	*progress_bar,
							 guint		 percentage);
gboolean	 pk_progress_bar_set_value		(PkProgressBar	*progress_bar,
							 guint		 value);
void		 pk_progress_bar_set_allow_cancel	(PkProgressBar	*progress_bar,
							 gboolean	 allow_cancel);
gboolean	 pk_progress_bar_start			(PkProgressBar	*progress_bar,
							 const gchar	*text);
gboolean	 pk_progress_bar_end			(PkProgressBar	*progress_bar);

G_END_DECLS

#endif /* __PK_PROGRESS_BAR_H */
