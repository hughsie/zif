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
#include "dum-string.h"

/**
 * dum_string_new:
 **/
DumString *
dum_string_new (const gchar *value)
{
	DumString *string;
	string = g_new0 (DumString, 1);
	string->count = 1;
	string->value = g_strdup (value);
	return string;
}

/**
 * dum_string_new_value:
 **/
DumString *
dum_string_new_value (gchar *value)
{
	DumString *string;
	string = g_new0 (DumString, 1);
	string->count = 1;
	string->value = value;
	return string;
}

/**
 * dum_string_ref:
 **/
DumString *
dum_string_ref (DumString *string)
{
	g_return_val_if_fail (string != NULL, NULL);
	string->count++;
	return string;
}

/**
 * dum_string_unref:
 **/
DumString *
dum_string_unref (DumString *string)
{
	if (string == NULL)
		return NULL;
	string->count--;
	if (string->count == 0) {
		g_free (string->value);
		g_free (string);
		string = NULL;
	}
	return string;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_string_test (EggTest *test)
{
	DumString *string;

	if (!egg_test_start (test, "DumString"))
		return;

	/************************************************************/
	egg_test_title (test, "create");
	string = dum_string_new ("kernel");
	if (g_strcmp0 (string->value, "kernel") == 0 && string->count == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %s:%i", string->value, string->count);

	/************************************************************/
	egg_test_title (test, "ref");
	dum_string_ref (string);
	egg_test_assert (test, string->count == 2);

	/************************************************************/
	egg_test_title (test, "unref");
	dum_string_unref (string);
	egg_test_assert (test, string->count == 1);

	/************************************************************/
	egg_test_title (test, "unref");
	string = dum_string_unref (string);
	egg_test_assert (test, string == NULL);

	egg_test_end (test);
}
#endif

