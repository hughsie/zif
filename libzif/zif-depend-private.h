/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2011 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_DEPEND_PRIVATE_H
#define __ZIF_DEPEND_PRIVATE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "zif-depend.h"
#include "zif-string.h"

G_BEGIN_DECLS

ZifDepend		*zif_depend_new_from_data	(const gchar		**keys,
							 const gchar		**values);
void			 zif_depend_set_flag		(ZifDepend		*depend,
							 ZifDependFlag		 flag);
void			 zif_depend_set_name		(ZifDepend		*depend,
							 const gchar		*name);
void			 zif_depend_set_version		(ZifDepend		*depend,
							 const gchar		*version);
void			 zif_depend_set_name_str	(ZifDepend		*depend,
							 ZifString		*name);
void			 zif_depend_set_version_str	(ZifDepend		*depend,
							 ZifString		*version);

G_END_DECLS

#endif /* __ZIF_DEPEND_PRIVATE_H */

