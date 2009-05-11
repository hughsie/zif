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

#ifndef __DUM_UTILS_H
#define __DUM_UTILS_H

#include <glib-object.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

gboolean	 dum_init			(void);
void		 dum_list_print_array		(GPtrArray	*array);
PkPackageId	*dum_package_id_from_nevra	(const gchar	*name,
						 const gchar	*epoch,
						 const gchar	*version,
						 const gchar	*release,
						 const gchar	*arch,
						 const gchar	*data);
gboolean	 dum_boolean_from_text		(const gchar	*text);

G_END_DECLS

#endif /* __DUM_UTILS_H */

