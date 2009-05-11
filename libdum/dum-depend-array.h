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

#if !defined (__DUM_H_INSIDE__) && !defined (DUM_COMPILATION)
#error "Only <dum.h> can be included directly."
#endif

#ifndef __DUM_DEPEND_ARRAY_H
#define __DUM_DEPEND_ARRAY_H

#include <glib-object.h>

#include "dum-depend.h"

G_BEGIN_DECLS

typedef struct {
	GPtrArray	*value;
	guint		 count;
} DumDependArray;

DumDependArray	*dum_depend_array_new		(const GPtrArray	*value);
DumDependArray	*dum_depend_array_ref		(DumDependArray		*array);
DumDependArray	*dum_depend_array_unref		(DumDependArray		*array);
void		 dum_depend_array_add		(DumDependArray		*array,
						 DumDepend		*depend);

G_END_DECLS

#endif /* __DUM_DEPEND_ARRAY_H */

