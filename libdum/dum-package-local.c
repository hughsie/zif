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
#include <packagekit-glib/packagekit.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmts.h>

#include "egg-debug.h"

#include "dum-utils.h"
#include "dum-package.h"
#include "dum-package-local.h"
#include "dum-groups.h"
#include "dum-string.h"
#include "dum-string-array.h"
#include "dum-depend.h"
#include "dum-depend-array.h"

#define DUM_PACKAGE_LOCAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_PACKAGE_LOCAL, DumPackageLocalPrivate))

struct DumPackageLocalPrivate
{
	DumGroups		*groups;
};

G_DEFINE_TYPE (DumPackageLocal, dum_package_local, DUM_TYPE_PACKAGE)

/**
 * dum_get_header_string:
 **/
static DumString *
dum_get_header_string (Header header, rpmTag tag)
{
	gint retval;
	DumString *data = NULL;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;
	data = dum_string_new (rpmtdGetString (td));
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return data;
}

/**
 * dum_get_header_u32:
 **/
static guint
dum_get_header_u32 (Header header, rpmTag tag)
{
	gint retval;
	uint32_t *data_p;
	guint data = 0;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, RPMTAG_SIZE, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;
	data_p = rpmtdGetUint32 (td);
	if (data_p != NULL)
		data = *data_p;
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return data;
}

/**
 * dum_get_header_uin32_index:
 **/
static GPtrArray *
dum_get_header_uin32_index (Header header, rpmTag tag, guint length)
{
	gint retval;
	guint32 *data;
	GPtrArray *array = NULL;
	rpmtd td;
	guint i;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;
	array = g_ptr_array_new ();
	data = rpmtdGetUint32 (td);
	for (i=0;i<length; i++)
		g_ptr_array_add (array, GUINT_TO_POINTER (*(data+i)));
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return array;
}

/**
 * dum_get_header_strv:
 **/
static DumStringArray *
dum_get_header_strv (Header header, rpmTag tag)
{
	gint retval;
	const gchar *data;
	DumStringArray *array = NULL;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;
	array = dum_string_array_new (NULL);
	data = rpmtdGetString (td);
	data = rpmtdNextString (td);
	while (data != NULL) {
		g_ptr_array_add (array->value, g_strdup (data));
		data = rpmtdNextString (td);
	}
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return array;
}

/**
 * dum_package_local_id_from_header:
 **/
static PkPackageId *
dum_package_local_id_from_header (Header header)
{
	PkPackageId *id;
	const gchar *name;
	gchar *epoch;
	const gchar *version;
	const gchar *release;
	const gchar *arch;
	uint32_t *epoch_p;

	/* get NEVRA, cannot fail */
	headerNEVRA (header, &name, &epoch_p, &version, &release, &arch);

	/* trivial */
	if (epoch_p == NULL) {
		id = dum_package_id_from_nevra (name, NULL, version, release, arch, "installed");
		goto out;
	}

	epoch = g_strdup_printf ("%i", *epoch_p);
	id = dum_package_id_from_nevra (name, epoch, version, release, arch, "installed");
	g_free (epoch);
out:
	return id;
}

/**
 * dum_package_local_get_depends_from_name_flags_version:
 **/
static DumDependArray *
dum_package_local_get_depends_from_name_flags_version (DumStringArray *names, GPtrArray *flags, DumStringArray *versions)
{
	guint i;
	rpmsenseFlags rpmflags;
	DumDepend *depend;
	DumDependFlag flag;
	const gchar *name;
	const gchar *version;
	DumDependArray *array;

	/* create requires */
	array = dum_depend_array_new (NULL);
	for (i=0; i<names->value->len; i++) {
		name = g_ptr_array_index (names->value, i);
		version = g_ptr_array_index (versions->value, i);

		/* no version string */
		if (version == NULL || version[0] == '\0') {
			depend = dum_depend_new (name, DUM_DEPEND_FLAG_ANY, NULL);
			dum_depend_array_add (array, depend);
			dum_depend_unref (depend);
			continue;
		}

		/* ignore rpmlib flags */
		rpmflags = GPOINTER_TO_UINT (g_ptr_array_index (flags, i));
		if ((rpmflags & RPMSENSE_RPMLIB) > 0)
			continue;

		/* convert to enums */
		flag = DUM_DEPEND_FLAG_UNKNOWN;
		if ((rpmflags & RPMSENSE_LESS) > 0) {
			flag = DUM_DEPEND_FLAG_LESS;
		} else if ((rpmflags & RPMSENSE_GREATER) > 0) {
			flag = DUM_DEPEND_FLAG_GREATER;
		} else if ((rpmflags & RPMSENSE_EQUAL) > 0) {
			flag = DUM_DEPEND_FLAG_EQUAL;
		}

		/* unknown */
		if (flag == DUM_DEPEND_FLAG_UNKNOWN) {
			egg_debug ("ignoring %s %s %s", name, dum_depend_flag_to_string (flag), version);
			continue;
		}

		depend = dum_depend_new (name, flag, version);
		dum_depend_array_add (array, depend);
		dum_depend_unref (depend);
	}
	return array;
}

/**
 * dum_package_local_set_from_header:
 **/
gboolean
dum_package_local_set_from_header (DumPackageLocal *pkg, Header header, GError **error)
{
	guint i;
	guint size;
	DumString *tmp;
	PkPackageId *id;
	PkGroupEnum group;
	GPtrArray *fileindex;
//	DumStringArray *tmparray;
	DumStringArray *files;
	DumStringArray *dirnames;
	DumStringArray *basenames;
	DumDependArray *depends;
//	DumStringArray *provides;

	GPtrArray *flags;
	DumStringArray *names;
	DumStringArray *versions;

	g_return_val_if_fail (DUM_IS_PACKAGE_LOCAL (pkg), FALSE);
	g_return_val_if_fail (header != NULL, FALSE);

	dum_package_set_installed (DUM_PACKAGE (pkg), TRUE);

	/* id */
	id = dum_package_local_id_from_header (header);
	dum_package_set_id (DUM_PACKAGE (pkg), id);
	pk_package_id_free (id);

	/* summary */
	tmp = dum_get_header_string (header, RPMTAG_SUMMARY);
	dum_package_set_summary (DUM_PACKAGE (pkg), tmp);
	dum_string_unref (tmp);

	/* license */
	tmp = dum_get_header_string (header, RPMTAG_LICENSE);
	dum_package_set_license (DUM_PACKAGE (pkg), tmp);
	dum_string_unref (tmp);

	/* description */
	tmp = dum_get_header_string (header, RPMTAG_DESCRIPTION);
	dum_package_set_description (DUM_PACKAGE (pkg), tmp);
	dum_string_unref (tmp);

	/* url */
	tmp = dum_get_header_string (header, RPMTAG_URL);
	if (tmp != NULL)
		dum_package_set_url (DUM_PACKAGE (pkg), tmp);
	dum_string_unref (tmp);

	/* size */
	size = dum_get_header_u32 (header, RPMTAG_SIZE);
	if (size != 0)
		dum_package_set_size (DUM_PACKAGE (pkg), size);

	/* category && group */
	tmp = dum_get_header_string (header, RPMTAG_GROUP);
	dum_package_set_category (DUM_PACKAGE (pkg), tmp);
	group = dum_groups_get_group_for_cat (pkg->priv->groups, tmp->value, NULL);
	if (group != PK_GROUP_ENUM_UNKNOWN)
		dum_package_set_group (DUM_PACKAGE (pkg), group);
	dum_string_unref (tmp);

	/* requires */
	names = dum_get_header_strv (header, RPMTAG_REQUIRENAME);
	if (names == NULL) {
		depends = dum_depend_array_new (NULL);
		dum_package_set_requires (DUM_PACKAGE (pkg), depends);
		dum_depend_array_unref (depends);
	} else {	
		versions = dum_get_header_strv (header, RPMTAG_REQUIREVERSION);
		flags = dum_get_header_uin32_index (header, RPMTAG_REQUIREFLAGS, names->value->len);
		depends = dum_package_local_get_depends_from_name_flags_version (names, flags, versions);
		dum_package_set_requires (DUM_PACKAGE (pkg), depends);
		dum_depend_array_unref (depends);
		dum_string_array_unref (names);
		dum_string_array_unref (versions);
		g_ptr_array_free (flags, TRUE);
	}

	/* provides */
	names = dum_get_header_strv (header, RPMTAG_PROVIDENAME);
	if (names == NULL) {
		depends = dum_depend_array_new (NULL);
		dum_package_set_provides (DUM_PACKAGE (pkg), depends);
		dum_depend_array_unref (depends);
	} else {	
		versions = dum_get_header_strv (header, RPMTAG_PROVIDEVERSION);
		flags = dum_get_header_uin32_index (header, RPMTAG_PROVIDEFLAGS, names->value->len);
		depends = dum_package_local_get_depends_from_name_flags_version (names, flags, versions);
		dum_package_set_provides (DUM_PACKAGE (pkg), depends);
		dum_depend_array_unref (depends);
		dum_string_array_unref (names);
		dum_string_array_unref (versions);
		g_ptr_array_free (flags, TRUE);
	}

	/* conflicts */
	names = dum_get_header_strv (header, RPMTAG_CONFLICTNAME);
	if (names == NULL) {
		depends = dum_depend_array_new (NULL);
		//dum_package_set_conflicts (DUM_PACKAGE (pkg), depends);
		dum_depend_array_unref (depends);
	} else {	
		versions = dum_get_header_strv (header, RPMTAG_CONFLICTVERSION);
		flags = dum_get_header_uin32_index (header, RPMTAG_CONFLICTFLAGS, names->value->len);
		depends = dum_package_local_get_depends_from_name_flags_version (names, flags, versions);
		//dum_package_set_conflicts (DUM_PACKAGE (pkg), depends);
		dum_depend_array_unref (depends);
		dum_string_array_unref (names);
		dum_string_array_unref (versions);
		g_ptr_array_free (flags, TRUE);
	}

	/* obsoletes */
	names = dum_get_header_strv (header, RPMTAG_OBSOLETENAME);
	if (names == NULL) {
		depends = dum_depend_array_new (NULL);
		//dum_package_set_obsoletes (DUM_PACKAGE (pkg), depends);
		dum_depend_array_unref (depends);
	} else {	
		versions = dum_get_header_strv (header, RPMTAG_OBSOLETEVERSION);
		flags = dum_get_header_uin32_index (header, RPMTAG_OBSOLETEFLAGS, names->value->len);
		depends = dum_package_local_get_depends_from_name_flags_version (names, flags, versions);
		//dum_package_set_obsoletes (DUM_PACKAGE (pkg), depends);
		dum_depend_array_unref (depends);
		dum_string_array_unref (names);
		dum_string_array_unref (versions);
		g_ptr_array_free (flags, TRUE);
	}

	/* files */
	basenames = dum_get_header_strv (header, RPMTAG_BASENAMES);

	/* create the files */
	if (basenames != NULL) {

		/* get the mapping */
		dirnames = dum_get_header_strv (header, RPMTAG_DIRNAMES);
		fileindex = dum_get_header_uin32_index (header, RPMTAG_DIRINDEXES, basenames->value->len);

		files = dum_string_array_new (NULL);
		for (i=0; i<basenames->value->len; i++) {
			guint idx;
			idx = GPOINTER_TO_UINT (g_ptr_array_index (fileindex, i));
			g_ptr_array_add (files->value, g_strconcat (g_ptr_array_index (dirnames->value, idx), g_ptr_array_index (basenames->value, i), NULL));
		}
		dum_package_set_files (DUM_PACKAGE (pkg), files);
		dum_string_array_unref (files);

		/* free, as we have files */
		dum_string_array_unref (dirnames);
		dum_string_array_unref (basenames);
		g_ptr_array_free (fileindex, TRUE);
	} else {
		files = dum_string_array_new (NULL);
		dum_package_set_files (DUM_PACKAGE (pkg), files);
		dum_string_array_unref (files);
	}

	return TRUE;
}

/**
 * dum_package_local_set_from_filename:
 **/
gboolean
dum_package_local_set_from_filename (DumPackageLocal *pkg, const gchar *filename, GError **error)
{
	int rc;
	FD_t fd = NULL;
	Header hdr = NULL;
	rpmts ts;
	gboolean ret = FALSE;
	GError *error_local = NULL;

	/* open the file for reading */
	fd = Fopen(filename, "r.fdio"); 
	if (fd == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to open %s", filename);
		goto out;
	}
	if (Ferror(fd)) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to open %s: %s", filename, Fstrerror(fd));
		goto out;
	}

	/* create an empty transaction set */
	ts = rpmtsCreate ();

	/* read in the file */
	rc = rpmReadPackageFile(ts, fd, "dum", &hdr);
	if (rc != RPMRC_OK) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to read %s", filename);
		goto out;
	}

	/* convert and upscale */
	headerConvert(hdr, HEADERCONV_RETROFIT_V3);

	/* set from header */
	ret = dum_package_local_set_from_header (pkg, hdr, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set from header: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* close the database used by the transaction */
	rc = rpmtsCloseDB (ts);
	if (rc != 0) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to close");
		ret = FALSE;
		goto out;
	}
out:
	/* close header and file */
	if (hdr != NULL)
		headerFree (hdr);
	if (fd != NULL)
		Fclose (fd);
	return ret;
}

/**
 * dum_package_local_finalize:
 **/
static void
dum_package_local_finalize (GObject *object)
{
	DumPackageLocal *pkg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_PACKAGE_LOCAL (object));
	pkg = DUM_PACKAGE_LOCAL (object);

	g_object_unref (pkg->priv->groups);

	G_OBJECT_CLASS (dum_package_local_parent_class)->finalize (object);
}

/**
 * dum_package_local_class_init:
 **/
static void
dum_package_local_class_init (DumPackageLocalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_package_local_finalize;
	g_type_class_add_private (klass, sizeof (DumPackageLocalPrivate));
}

/**
 * dum_package_local_init:
 **/
static void
dum_package_local_init (DumPackageLocal *pkg)
{
	pkg->priv = DUM_PACKAGE_LOCAL_GET_PRIVATE (pkg);
	pkg->priv->groups = dum_groups_new ();
}

/**
 * dum_package_local_new:
 * Return value: A new package_local class instance.
 **/
DumPackageLocal *
dum_package_local_new (void)
{
	DumPackageLocal *pkg;
	pkg = g_object_new (DUM_TYPE_PACKAGE_LOCAL, NULL);
	return DUM_PACKAGE_LOCAL (pkg);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_package_local_test (EggTest *test)
{
	DumPackageLocal *pkg;

	if (!egg_test_start (test, "DumPackageLocal"))
		return;

	/************************************************************/
	egg_test_title (test, "get package_local");
	pkg = dum_package_local_new ();
	egg_test_assert (test, pkg != NULL);

	g_object_unref (pkg);

	egg_test_end (test);
}
#endif

