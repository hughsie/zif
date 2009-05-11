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
#include "dum-string-array.h"

/**
 * dum_string_array_new:
 **/
DumStringArray *
dum_string_array_new (const GPtrArray *value)
{
	guint i;
	DumStringArray *array;

	array = g_new0 (DumStringArray, 1);
	array->count = 1;
	array->value = g_ptr_array_new ();

	/* nothing to copy */
	if (value == NULL)
		goto out;

	/* copy */
	for (i=0; i<value->len; i++)
		g_ptr_array_add (array->value, g_strdup (g_ptr_array_index (value, i)));
out:
	return array;
}

/**
 * dum_string_array_new_value:
 **/
DumStringArray *
dum_string_array_new_value (GPtrArray *value)
{
	DumStringArray *array;
	array = g_new0 (DumStringArray, 1);
	array->count = 1;
	array->value = value;
	return array;
}

/**
 * dum_string_array_ref:
 **/
DumStringArray *
dum_string_array_ref (DumStringArray *array)
{
	g_return_val_if_fail (array != NULL, NULL);
	array->count++;
	return array;
}

/**
 * dum_string_array_unref:
 **/
DumStringArray *
dum_string_array_unref (DumStringArray *array)
{
	if (array == NULL)
		return NULL;
	array->count--;
	if (array->count == 0) {
		g_ptr_array_foreach (array->value, (GFunc) g_free, NULL);
		g_ptr_array_free (array->value, TRUE);
		g_free (array);
		array = NULL;
	}
	return array;
}

/**
 * dum_string_array_unique:
 *
 * Optimised for large lists...
 **/
DumStringArray *
dum_string_array_unique (DumStringArray *array)
{
	guint i;
	const gchar *value;
	DumStringArray *new;
	GHashTable *hash;
	gpointer found;

	/* use a hash table for blistering speed */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	/* create a new array without duplicates */
	new = dum_string_array_new (NULL);
	for (i=0; i<array->value->len; i++) {
		value = g_ptr_array_index (array->value, i);
		found = g_hash_table_lookup (hash, value);
		if (found == NULL) {
			g_ptr_array_add (new->value, g_strdup (value));
			g_hash_table_insert (hash, g_strdup (value), GUINT_TO_POINTER (1));
		}
	}
	g_hash_table_unref (hash);
	return new;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_string_array_test (EggTest *test)
{
	DumStringArray *array;

	if (!egg_test_start (test, "DumStringArray"))
		return;

	/************************************************************/
	egg_test_title (test, "create");
	array = dum_string_array_new (NULL);
	if (array->value->len == 0 && array->count == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %s:%i", array->value, array->count);

	/************************************************************/
	egg_test_title (test, "ref");
	dum_string_array_ref (array);
	egg_test_assert (test, array->count == 2);

	/************************************************************/
	egg_test_title (test, "unref");
	dum_string_array_unref (array);
	egg_test_assert (test, array->count == 1);

	/************************************************************/
	egg_test_title (test, "unref");
	array = dum_string_array_unref (array);
	egg_test_assert (test, array == NULL);

	egg_test_end (test);
}
#endif

