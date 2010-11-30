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

/**
 * SECTION:zif-array
 * @short_description: An a rray that stores refcounted #GObject's.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-object-array.h"

/**
 * zif_object_array_add:
 * @array: A #GPtrArray
 * @object: A #GObject to store in the array
 *
 * Adds an object to the array.
 * The object is refcounted internally.
 *
 * Since: 0.1.3
 **/
void
zif_object_array_add (GPtrArray *array, gpointer object)
{
	g_return_if_fail (array != NULL);
	g_return_if_fail (object != NULL);
	g_ptr_array_add (array, g_object_ref (G_OBJECT (object)));
}

/**
 * zif_object_array_add_array:
 * @array: A #GPtrArray
 * @source: A object to store in the array
 *
 * Adds an object to the array.
 * The object is refcounted internally.
 *
 * Since: 0.1.3
 **/
void
zif_object_array_add_array (GPtrArray *array, GPtrArray *source)
{
	guint i;
	GObject *temp;

	g_return_if_fail (array != NULL);
	g_return_if_fail (source != NULL);

	for (i=0; i<source->len; i++) {
		temp = g_ptr_array_index (source, i);
		g_ptr_array_add (array, g_object_ref (temp));
	}
}

/**
 * zif_object_array_new:
 *
 * Return value: A new #GPtrArray instance, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_object_array_new (void)
{
	return g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);;
}

