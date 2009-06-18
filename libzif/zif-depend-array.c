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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "egg-debug.h"
#include "zif-depend-array.h"

/* private structure */
struct ZifDependArray {
	GPtrArray	*value;
	guint		 count;
};

/**
 * zif_depend_array_new:
 **/
ZifDependArray *
zif_depend_array_new (const GPtrArray *value)
{
	guint i;
	ZifDependArray *array;

	array = g_new0 (ZifDependArray, 1);
	array->count = 1;
	array->value = g_ptr_array_new ();

	/* nothing to copy */
	if (value == NULL)
		goto out;

	/* copy */
	for (i=0; i<value->len; i++)
		g_ptr_array_add (array->value, zif_depend_ref (g_ptr_array_index (value, i)));
out:
	return array;
}

/**
 * zif_depend_array_ref:
 **/
ZifDependArray *
zif_depend_array_ref (ZifDependArray *array)
{
	g_return_val_if_fail (array != NULL, NULL);
	array->count++;
	return array;
}

/**
 * zif_depend_array_unref:
 **/
ZifDependArray *
zif_depend_array_unref (ZifDependArray *array)
{
#ifdef ZIF_CRASH_DEBUG
	if (array == NULL)
		array->count = 999;
#endif
	g_return_val_if_fail (array != NULL, NULL);
	array->count--;
	if (array->count == 0) {
		g_ptr_array_foreach (array->value, (GFunc) zif_depend_unref, NULL);
		g_ptr_array_free (array->value, TRUE);
		g_free (array);
		array = NULL;
	}
	return array;
}

/**
 * zif_depend_array_add:
 **/
void
zif_depend_array_add (ZifDependArray *array, ZifDepend *depend)
{
	g_return_if_fail (array != NULL);
	g_return_if_fail (depend != NULL);
	g_ptr_array_add (array->value, zif_depend_ref (depend));
}

/**
 * zif_depend_array_get_length:
 * @array: the #ZifDependArray object
 *
 * Returns the size of the #ZifDependArray.
 *
 * Return value: the array length
 **/
guint
zif_depend_array_get_length (ZifDependArray *array)
{
	g_return_val_if_fail (array != NULL, 0);
	return array->value->len;
}

/**
 * zif_depend_array_get_value:
 * @array: the #ZifDependArray object
 * @index: the position to retrieve
 *
 * Returns the depend stored in the #ZifDependArray at the index.
 * This value is only valid while the #ZifDependArray's reference count > 1.
 *
 * Return value: depend value
 **/
const ZifDepend *
zif_depend_array_get_value (ZifDependArray *array, guint index)
{
	g_return_val_if_fail (array != NULL, NULL);
	g_return_val_if_fail (index < array->value->len, NULL);
	return g_ptr_array_index (array->value, index);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_depend_array_test (EggTest *test)
{
	ZifDependArray *array;

	if (!egg_test_start (test, "ZifDependArray"))
		return;

	/************************************************************/
	egg_test_title (test, "create");
	array = zif_depend_array_new (NULL);
	if (array->value->len == 0 && array->count == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %s:%i", array->value, array->count);

	/************************************************************/
	egg_test_title (test, "ref");
	zif_depend_array_ref (array);
	egg_test_assert (test, array->count == 2);

	/************************************************************/
	egg_test_title (test, "unref");
	zif_depend_array_unref (array);
	egg_test_assert (test, array->count == 1);

	/************************************************************/
	egg_test_title (test, "unref");
	array = zif_depend_array_unref (array);
	egg_test_assert (test, array == NULL);

	egg_test_end (test);
}
#endif

