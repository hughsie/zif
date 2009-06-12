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
#include "zif-depend.h"

/**
 * zif_depend_flag_to_string:
 **/
const gchar *
zif_depend_flag_to_string (ZifDependFlag flag)
{
	if (flag == ZIF_DEPEND_FLAG_ANY)
		return "~";
	if (flag == ZIF_DEPEND_FLAG_LESS)
		return "<";
	if (flag == ZIF_DEPEND_FLAG_GREATER)
		return ">";
	if (flag == ZIF_DEPEND_FLAG_EQUAL)
		return "=";
	return "unknown";
}

/**
 * zif_depend_new:
 **/
ZifDepend *
zif_depend_new (const gchar *name, ZifDependFlag flag, const gchar *version)
{
	ZifDepend *depend;
	depend = g_new0 (ZifDepend, 1);
	depend->count = 1;
	depend->name = g_strdup (name);
	depend->flag = flag;
	depend->version = g_strdup (version);
	return depend;
}

/**
 * zif_depend_new_value:
 **/
ZifDepend *
zif_depend_new_value (gchar *name, ZifDependFlag flag, gchar *version)
{
	ZifDepend *depend;
	depend = g_new0 (ZifDepend, 1);
	depend->count = 1;
	depend->name = name;
	depend->flag = flag;
	depend->version = version;
	return depend;
}

/**
 * zif_depend_ref:
 **/
ZifDepend *
zif_depend_ref (ZifDepend *depend)
{
	g_return_val_if_fail (depend != NULL, NULL);
	depend->count++;
	return depend;
}

/**
 * zif_depend_unref:
 **/
ZifDepend *
zif_depend_unref (ZifDepend *depend)
{
	if (depend == NULL)
		return NULL;
	depend->count--;
	if (depend->count == 0) {
		g_free (depend->name);
		g_free (depend->version);
		g_free (depend);
		depend = NULL;
	}
	return depend;
}

/**
 * zif_depend_to_string:
 **/
gchar *
zif_depend_to_string (const ZifDepend *depend)
{
	g_return_val_if_fail (depend != NULL, NULL);
	if (depend->version == NULL)
		return g_strdup (depend->name);
	return g_strdup_printf ("%s %s %s", depend->name, zif_depend_flag_to_string (depend->flag), depend->version);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_depend_test (EggTest *test)
{
	ZifDepend *depend;

	if (!egg_test_start (test, "ZifDepend"))
		return;

	/************************************************************/
	egg_test_title (test, "create");
	depend = zif_depend_new ("kernel", ZIF_DEPEND_FLAG_GREATER, "2.6.0");
	if (g_strcmp0 (depend->name, "kernel") == 0 && depend->count == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %s:%i", depend->name, depend->count);

	/************************************************************/
	egg_test_title (test, "ref");
	zif_depend_ref (depend);
	egg_test_assert (test, depend->count == 2);

	/************************************************************/
	egg_test_title (test, "unref");
	zif_depend_unref (depend);
	egg_test_assert (test, depend->count == 1);

	/************************************************************/
	egg_test_title (test, "unref");
	depend = zif_depend_unref (depend);
	egg_test_assert (test, depend == NULL);

	egg_test_end (test);
}
#endif

