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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "dum-utils.h"
#include "dum-package.h"

/**
 * dum_init:
 **/
gboolean
dum_init (void)
{
	gint retval;

	retval = rpmReadConfigFiles (NULL, NULL);
	if (retval != 0) {
		egg_warning ("failed to read config files");
		return FALSE;
	}

	return TRUE;
}

/**
 * dum_boolean_from_text:
 **/
gboolean
dum_boolean_from_text (const gchar *text)
{
	g_return_val_if_fail (text != NULL, FALSE);
	if (g_ascii_strcasecmp (text, "true") == 0 ||
	    g_ascii_strcasecmp (text, "yes") == 0 ||
	    g_ascii_strcasecmp (text, "1") == 0)
		return TRUE;
	return FALSE;
}

/**
 * dum_list_print_array:
 **/
void
dum_list_print_array (GPtrArray *array)
{
	guint i;
	DumPackage *package;

	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);
		dum_package_print (package);
	}
}

/**
 * dum_package_id_from_header:
 **/
PkPackageId *
dum_package_id_from_nevra (const gchar *name, const gchar *epoch, const gchar *version, const gchar *release, const gchar *arch, const gchar *data)
{
	gchar *version_compound;
	PkPackageId *id;

	/* do we include an epoch? */
	if (epoch == NULL)
		version_compound = g_strdup_printf ("%s-%s", version, release);
	else
		version_compound = g_strdup_printf ("%s:%s-%s", epoch, version, release);

	id = pk_package_id_new_from_list (name, version_compound, arch, data);
	g_free (version_compound);
	return id;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_utils_test (EggTest *test)
{
	PkPackageId *id;
	gchar *package_id;
	gboolean ret;

	if (!egg_test_start (test, "DumUtils"))
		return;

	/************************************************************
	 ****************           NEVRA          ******************
	 ************************************************************/
	egg_test_title (test, "no epoch");
	id = dum_package_id_from_nevra ("kernel", NULL, "0.0.1", "1", "i386", "fedora");
	package_id = pk_package_id_to_string (id);
	if (egg_strequal (package_id, "kernel;0.0.1-1;i386;fedora"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect package_id '%s'", package_id);
	g_free (package_id);
	pk_package_id_free (id);

	/************************************************************/
	egg_test_title (test, "epoch");
	id = dum_package_id_from_nevra ("kernel", "2", "0.0.1", "1", "i386", "fedora");
	package_id = pk_package_id_to_string (id);
	if (egg_strequal (package_id, "kernel;2:0.0.1-1;i386;fedora"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect package_id '%s'", package_id);
	g_free (package_id);
	pk_package_id_free (id);

	/************************************************************/
	egg_test_title (test, "init");
	ret = dum_init ();
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "bool to text true (1)");
	ret = dum_boolean_from_text ("1");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "bool to text true (2)");
	ret = dum_boolean_from_text ("TRUE");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "bool to text false");
	ret = dum_boolean_from_text ("false");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "bool to text blank");
	ret = dum_boolean_from_text ("");
	egg_test_assert (test, !ret);

	egg_test_end (test);
}
#endif

