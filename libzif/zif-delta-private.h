/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either checksum 2 of the License, or
 * (at your option) any later checksum.
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

#ifndef __ZIF_DELTA_PRIVATE_H
#define __ZIF_DELTA_PRIVATE_H

#include <glib-object.h>
#include "zif-delta.h"

G_BEGIN_DECLS

void			 zif_delta_set_id		(ZifDelta		*delta,
							 const gchar		*id);
void			 zif_delta_set_size		(ZifDelta		*delta,
							 guint64		 size);
void			 zif_delta_set_filename		(ZifDelta		*delta,
							 const gchar		*filename);
void			 zif_delta_set_sequence		(ZifDelta		*delta,
							 const gchar		*sequence);
void			 zif_delta_set_checksum		(ZifDelta		*delta,
							 const gchar		*checksum);

G_END_DECLS

#endif /* __ZIF_DELTA_PRIVATE_H */

