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

#include "zif-utils.h"
#include "zif-package.h"

/**
 * zif_init:
 **/
gboolean
zif_init (void)
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
 * zif_boolean_from_text:
 **/
gboolean
zif_boolean_from_text (const gchar *text)
{
	g_return_val_if_fail (text != NULL, FALSE);
	if (g_ascii_strcasecmp (text, "true") == 0 ||
	    g_ascii_strcasecmp (text, "yes") == 0 ||
	    g_ascii_strcasecmp (text, "1") == 0)
		return TRUE;
	return FALSE;
}

/**
 * zif_list_print_array:
 **/
void
zif_list_print_array (GPtrArray *array)
{
	guint i;
	ZifPackage *package;

	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);
		zif_package_print (package);
	}
}

/**
 * zif_package_id_from_header:
 **/
PkPackageId *
zif_package_id_from_nevra (const gchar *name, const gchar *epoch, const gchar *version, const gchar *release, const gchar *arch, const gchar *data)
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

/**
 * zif_package_convert_evr:
 *
 * Modifies evr, so pass in copy
 **/
static gboolean
zif_package_convert_evr (gchar *evr, const gchar **epoch, const gchar **version, const gchar **release)
{
	gchar *find;

	g_return_val_if_fail (evr != NULL, FALSE);

	/* set to NULL initially */
	*version = NULL;

	/* split possible epoch */
	find = strstr (evr, ":");
	if (find != NULL) {
		*find = '\0';
		*epoch = evr;
		*version = find+1;
	} else {
		*epoch = NULL;
		*version = evr;
	}

	/* split possible release */
	find = g_strrstr (*version, "-");
	if (find != NULL) {
		*find = '\0';
		*release = find+1;
	} else {
		*release = NULL;
	}

	return TRUE;
}

/**
 * zif_compare_evr:
 *
 * compare two [epoch:]version[-release]
 **/
gint
zif_compare_evr (const gchar *a, const gchar *b)
{
	gint val = 0;
	gchar *ad = NULL;
	gchar *bd = NULL;
	const gchar *ae, *av, *ar;
	const gchar *be, *bv, *br;

	g_return_val_if_fail (a != NULL, 0);
	g_return_val_if_fail (b != NULL, 0);

	/* exactly the same, optimise */
	if (strcmp (a, b) == 0)
		goto out;

	/* copy */
	ad = g_strdup (a);
	bd = g_strdup (b);

	/* split */
	zif_package_convert_evr (ad, &ae, &av, &ar);
	zif_package_convert_evr (bd, &be, &bv, &br);

	/* compare epoch */
	if (ae != NULL && be != NULL)
		val = rpmvercmp (ae, be);
	else if (ae != NULL && atol (ae) > 0) {
		val = 1;
		goto out;
	} else if (be != NULL && atol (be) > 0) {
		val = -1;
		goto out;
	}

	/* compare version */
	val = rpmvercmp (av, bv);
	if (val != 0)
		goto out;

	/* compare release */
	if (ar != NULL && br != NULL)
		val = rpmvercmp (ar, br);

out:
	g_free (ad);
	g_free (bd);
	return val;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_utils_test (EggTest *test)
{
	PkPackageId *id;
	gchar *package_id;
	gboolean ret;
	gchar *evr;
	gint val;
	const gchar *e;
	const gchar *v;
	const gchar *r;

	if (!egg_test_start (test, "ZifUtils"))
		return;

	/************************************************************
	 ****************           NEVRA          ******************
	 ************************************************************/
	egg_test_title (test, "no epoch");
	id = zif_package_id_from_nevra ("kernel", NULL, "0.0.1", "1", "i386", "fedora");
	package_id = pk_package_id_to_string (id);
	if (egg_strequal (package_id, "kernel;0.0.1-1;i386;fedora"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect package_id '%s'", package_id);
	g_free (package_id);
	pk_package_id_free (id);

	/************************************************************/
	egg_test_title (test, "epoch");
	id = zif_package_id_from_nevra ("kernel", "2", "0.0.1", "1", "i386", "fedora");
	package_id = pk_package_id_to_string (id);
	if (egg_strequal (package_id, "kernel;2:0.0.1-1;i386;fedora"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect package_id '%s'", package_id);
	g_free (package_id);
	pk_package_id_free (id);

	/************************************************************/
	egg_test_title (test, "init");
	ret = zif_init ();
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "bool to text true (1)");
	ret = zif_boolean_from_text ("1");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "bool to text true (2)");
	ret = zif_boolean_from_text ("TRUE");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "bool to text false");
	ret = zif_boolean_from_text ("false");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "bool to text blank");
	ret = zif_boolean_from_text ("");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "convert evr");
	evr = g_strdup ("7:1.0.0-6");
	zif_package_convert_evr (evr, &e, &v, &r);
	if (egg_strequal (e, "7") && egg_strequal (v, "1.0.0") && egg_strequal (r, "6"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect evr '%s','%s','%s'", e, v, r);
	g_free (evr);

	/************************************************************/
	egg_test_title (test, "convert evr no epoch");
	evr = g_strdup ("1.0.0-6");
	zif_package_convert_evr (evr, &e, &v, &r);
	if (e == NULL && egg_strequal (v, "1.0.0") && egg_strequal (r, "6"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect evr '%s','%s','%s'", e, v, r);
	g_free (evr);

	/************************************************************/
	egg_test_title (test, "convert evr no epoch or release");
	evr = g_strdup ("1.0.0");
	zif_package_convert_evr (evr, &e, &v, &r);
	if (e == NULL && egg_strequal (v, "1.0.0") && r == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect evr '%s','%s','%s'", e, v, r);
	g_free (evr);

	/************************************************************/
	egg_test_title (test, "compare same");
	val = zif_compare_evr ("1:1.0.2-3", "1:1.0.2-3");
	egg_test_assert (test, (val == 0));

	/************************************************************/
	egg_test_title (test, "compare right heavy");
	val = zif_compare_evr ("1:1.0.2-3", "1:1.0.2-4");
	egg_test_assert (test, (val == -1));

	/************************************************************/
	egg_test_title (test, "compare new release");
	val = zif_compare_evr ("1:1.0.2-4", "1:1.0.2-3");
	egg_test_assert (test, (val == 1));

	/************************************************************/
	egg_test_title (test, "compare new epoch");
	val = zif_compare_evr ("1:0.0.1-1", "1.0.2-2");
	egg_test_assert (test, (val == 1));

	/************************************************************/
	egg_test_title (test, "compare new version");
	val = zif_compare_evr ("1.0.2-1", "1.0.1-1");
	egg_test_assert (test, (val == 1));

	egg_test_end (test);
}
#endif

