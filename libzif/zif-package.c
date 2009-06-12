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
#include <string.h>
#include <stdlib.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"

#include "zif-utils.h"
#include "zif-package.h"
#include "zif-repos.h"
#include "zif-groups.h"
#include "zif-string.h"
#include "zif-string-array.h"
#include "zif-depend-array.h"

#define ZIF_PACKAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_PACKAGE, ZifPackagePrivate))

struct ZifPackagePrivate
{
	ZifGroups		*groups;
	ZifRepos		*repos;
	PkPackageId		*id;
	gchar			*id_txt;
	ZifString		*summary;
	ZifString		*description;
	ZifString		*license;
	ZifString		*url;
	ZifString		*category;
	ZifString		*location_href;
	PkGroupEnum		 group;
	guint64			 size;
	ZifStringArray		*files;
	ZifDependArray		*requires;
	ZifDependArray		*provides;
	gboolean		 installed;
};

G_DEFINE_TYPE (ZifPackage, zif_package, G_TYPE_OBJECT)

/**
 * zif_package_compare:
 **/
gint
zif_package_compare (ZifPackage *a, ZifPackage *b)
{
	const PkPackageId *ida;
	const PkPackageId *idb;
	gint val = 0;

	g_return_val_if_fail (ZIF_IS_PACKAGE (a), 0);
	g_return_val_if_fail (ZIF_IS_PACKAGE (b), 0);

	/* shallow copy */
	ida = zif_package_get_id (a);
	idb = zif_package_get_id (b);

	/* check name the same */
	if (g_strcmp0 (ida->name, idb->name) != 0) {
		egg_warning ("comparing between %s and %s", ida->name, idb->name);
		goto out;
	}

	/* do a version compare */
	val = zif_compare_evr (ida->version, idb->version);
out:
	return val;
}

/**
 * zif_package_download:
 **/
gboolean
zif_package_download (ZifPackage *package, const gchar *directory, GError **error)
{
	gboolean ret = FALSE;
	ZifStoreRemote *repo = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);
	g_return_val_if_fail (package->priv->id != NULL, FALSE);

	/* check we are not installed */
	if (package->priv->installed) {
		if (error != NULL)
			*error = g_error_new (1, 0, "cannot download installed packages");
		goto out;
	}

	/* find correct repo */
	repo = zif_repos_get_store (package->priv->repos, package->priv->id->data, &error_local);
	if (repo == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "cannot find remote repo: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* download from the repo */
	ret = zif_store_remote_download (repo, zif_string_get_value (package->priv->location_href), directory, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "cannot download from repo: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	if (repo != NULL)
		g_object_unref (repo);
	return ret;
}

/**
 * zif_package_print:
 **/
void
zif_package_print (ZifPackage *package)
{
	guint i;
	gchar *text;
	guint len;
	const ZifDepend *depend;

	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (package->priv->id != NULL);

	g_print ("id=%s\n", package->priv->id_txt);
	g_print ("summary=%s\n", zif_string_get_value (package->priv->summary));
	g_print ("description=%s\n", zif_string_get_value (package->priv->description));
	g_print ("license=%s\n", zif_string_get_value (package->priv->license));
	g_print ("group=%s\n", pk_group_enum_to_text (package->priv->group));
	g_print ("category=%s\n", zif_string_get_value (package->priv->category));
	if (package->priv->url != NULL)
		g_print ("url=%s\n", zif_string_get_value (package->priv->url));
	g_print ("size=%"G_GUINT64_FORMAT"\n", package->priv->size);

	if (package->priv->files != NULL) {
		g_print ("files:\n");
		len = zif_string_array_get_length (package->priv->files);
		for (i=0; i<len; i++)
			g_print ("\t%s\n", zif_string_array_get_value (package->priv->files, i));
	}
	if (package->priv->requires != NULL) {
		g_print ("requires:\n");
		len = zif_depend_array_get_length (package->priv->requires);
		for (i=0; i<len; i++) {
			depend = zif_depend_array_get_value (package->priv->requires, i);
			text = zif_depend_to_string (depend);
			g_print ("\t%s\n", text);
			g_free (text);
		}
	}
	if (package->priv->provides != NULL) {
		g_print ("provides:\n");
		len = zif_depend_array_get_length (package->priv->provides);
		for (i=0; i<len; i++) {
			depend = zif_depend_array_get_value (package->priv->provides, i);
			text = zif_depend_to_string (depend);
			g_print ("\t%s\n", text);
			g_free (text);
		}
	}
}

/**
 * zif_package_is_devel:
 **/
gboolean
zif_package_is_devel (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->id != NULL, FALSE);

	if (g_str_has_suffix (package->priv->id->name, "-debuginfo"))
		return TRUE;
	if (g_str_has_suffix (package->priv->id->name, "-devel"))
		return TRUE;
	if (g_str_has_suffix (package->priv->id->name, "-static"))
		return TRUE;
	if (g_str_has_suffix (package->priv->id->name, "-libs"))
		return TRUE;
	return FALSE;
}

/**
 * zif_package_is_gui:
 **/
gboolean
zif_package_is_gui (ZifPackage *package)
{
	guint i;
	guint len;
	const ZifDepend *depend;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->id != NULL, FALSE);

	/* trivial */
	if (package->priv->requires == NULL)
		return FALSE;

	len = zif_depend_array_get_length (package->priv->requires);
	for (i=0; i<len; i++) {
		depend = zif_depend_array_get_value (package->priv->requires, i);
		if (strstr (depend->name, "gtk") != NULL)
			return TRUE;
		if (strstr (depend->name, "kde") != NULL)
			return TRUE;
	}
	return FALSE;
}

/**
 * zif_package_is_installed:
 **/
gboolean
zif_package_is_installed (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->id != NULL, FALSE);
	return package->priv->installed;
}

/**
 * zif_package_is_free:
 *
 * Check the string license_text for free licenses, indicated by
 * their short names as documented at
 * http://fedoraproject.org/wiki/Licensing
 *
 * Licenses can be grouped by " or " to indicate that the package
 * can be redistributed under any of the licenses in the group.
 * For instance: GPLv2+ or Artistic or FooLicense.
 *
 * Also, if a license ends with "+", the "+" is removed before
 * comparing it to the list of valid licenses.  So if license
 * "FooLicense" is free, then "FooLicense+" is considered free.
 *
 * Groups of licenses can be grouped with " and " to indicate
 * that parts of the package are distributed under one group of
 * licenses, while other parts of the package are distributed
 * under another group.  Groups may be wrapped in parenthesis.
 * For instance:
 * (GPLv2+ or Artistic) and (GPL+ or Artistic) and FooLicense.
 *
 * At least one license in each group must be free for the
 * package to be considered Free Software.  If the license_text
 * is empty, the package is considered non-free.
 **/
gboolean
zif_package_is_free (ZifPackage *package)
{
	gboolean one_free_group = FALSE;
	gboolean group_is_free;
	gchar **groups;
	gchar **licenses;
	guint len;
	guint i;
	guint j;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->id != NULL, FALSE);

	/* split AND clase */
	groups = g_strsplit (zif_string_get_value (package->priv->license), " and ", 0);
	len = g_strv_length (groups);

	for (i=0; groups[i] != NULL; i++) {
		/* remove grouping */
		g_strdelimit (groups[i], "()", ' ');

		/* split OR clase */
		licenses = g_strsplit (groups[i], " or ", 0);

		group_is_free = FALSE;
		for (j=0; licenses[j] != NULL; j++) {

			/* remove 'and later' */
			g_strdelimit (licenses[j], "+", ' ');
			g_strstrip (licenses[j]);

			/* nothing to process */
			if (licenses[j][0] == '\0')
				continue;

			/* a valid free license */
			if (pk_license_enum_from_text (licenses[j]) != PK_LICENSE_ENUM_UNKNOWN) {
				one_free_group = TRUE;
				group_is_free = TRUE;
				break;
			}
		}
		g_strfreev (licenses);

		if (!group_is_free)
			return FALSE;
	}
	g_strfreev (groups);

	if (!one_free_group)
		return FALSE;

	return TRUE;
}

/**
 * zif_package_get_id:
 **/
const PkPackageId *
zif_package_get_id (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return package->priv->id;
}

/**
 * zif_package_get_package_id:
 **/
const gchar *
zif_package_get_package_id (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return package->priv->id_txt;
}

/**
 * zif_package_get_summary:
 **/
ZifString *
zif_package_get_summary (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return zif_string_ref (package->priv->summary);
}

/**
 * zif_package_get_description:
 **/
ZifString *
zif_package_get_description (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return zif_string_ref (package->priv->description);
}

/**
 * zif_package_get_license:
 **/
ZifString *
zif_package_get_license (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return zif_string_ref (package->priv->license);
}

/**
 * zif_package_get_url:
 **/
ZifString *
zif_package_get_url (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return zif_string_ref (package->priv->url);
}

/**
 * zif_package_get_filename:
 * e.g. Packages/net-snmp-5.4.2-3.fc10.i386.rpm
 **/
ZifString *
zif_package_get_filename (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return zif_string_ref (package->priv->location_href);
}

/**
 * zif_package_get_category:
 **/
ZifString *
zif_package_get_category (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return zif_string_ref (package->priv->category);
}

/**
 * zif_package_get_group:
 **/
PkGroupEnum
zif_package_get_group (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (package->priv->id != NULL, PK_GROUP_ENUM_UNKNOWN);
	return package->priv->group;
}

/**
 * zif_package_get_size:
 **/
guint64
zif_package_get_size (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), 0);
	g_return_val_if_fail (package->priv->id != NULL, 0);
	return package->priv->size;
}

/**
 * zif_package_get_files:
 **/
ZifStringArray *
zif_package_get_files (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	if (package->priv->files == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "no data for %s", package->priv->id->name);
	}
	return package->priv->files;
}

/**
 * zif_package_get_requires:
 **/
ZifDependArray *
zif_package_get_requires (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return package->priv->requires;
}

/**
 * zif_package_get_provides:
 **/
ZifDependArray *
zif_package_get_provides (ZifPackage *package, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return package->priv->provides;
}

/**
 * zif_package_set_installed:
 **/
gboolean
zif_package_set_installed (ZifPackage *package, gboolean installed)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	package->priv->installed = installed;
	return TRUE;
}

/**
 * zif_package_set_id:
 **/
gboolean
zif_package_set_id (ZifPackage *package, const PkPackageId *id)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (package->priv->id == NULL, FALSE);

	package->priv->id = pk_package_id_copy (id);
	package->priv->id_txt = pk_package_id_to_string (package->priv->id);
	return TRUE;
}

/**
 * zif_package_set_summary:
 **/
gboolean
zif_package_set_summary (ZifPackage *package, ZifString *summary)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (summary != NULL, FALSE);
	g_return_val_if_fail (package->priv->summary == NULL, FALSE);

	package->priv->summary = zif_string_ref (summary);
	return TRUE;
}

/**
 * zif_package_set_description:
 **/
gboolean
zif_package_set_description (ZifPackage *package, ZifString *description)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (description != NULL, FALSE);
	g_return_val_if_fail (package->priv->description == NULL, FALSE);

	package->priv->description = zif_string_ref (description);
	return TRUE;
}

/**
 * zif_package_set_license:
 **/
gboolean
zif_package_set_license (ZifPackage *package, ZifString *license)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (license != NULL, FALSE);
	g_return_val_if_fail (package->priv->license == NULL, FALSE);

	package->priv->license = zif_string_ref (license);
	return TRUE;
}

/**
 * zif_package_set_url:
 **/
gboolean
zif_package_set_url (ZifPackage *package, ZifString *url)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (url != NULL, FALSE);
	g_return_val_if_fail (package->priv->url == NULL, FALSE);

	package->priv->url = zif_string_ref (url);
	return TRUE;
}

/**
 * zif_package_set_location_href:
 **/
gboolean
zif_package_set_location_href (ZifPackage *package, ZifString *location_href)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (location_href != NULL, FALSE);
	g_return_val_if_fail (package->priv->location_href == NULL, FALSE);

	package->priv->location_href = zif_string_ref (location_href);
	return TRUE;
}

/**
 * zif_package_set_category:
 **/
gboolean
zif_package_set_category (ZifPackage *package, ZifString *category)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (category != NULL, FALSE);
	g_return_val_if_fail (package->priv->category == NULL, FALSE);

	package->priv->category = zif_string_ref (category);
	return TRUE;
}

/**
 * zif_package_set_group:
 **/
gboolean
zif_package_set_group (ZifPackage *package, PkGroupEnum group)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (group != PK_GROUP_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (package->priv->group == PK_GROUP_ENUM_UNKNOWN, FALSE);

	package->priv->group = group;
	return TRUE;
}

/**
 * zif_package_set_size:
 **/
gboolean
zif_package_set_size (ZifPackage *package, guint64 size)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (size != 0, FALSE);
	g_return_val_if_fail (package->priv->size == 0, FALSE);

	package->priv->size = size;
	return TRUE;
}

/**
 * zif_package_set_files:
 **/
gboolean
zif_package_set_files (ZifPackage *package, ZifStringArray *files)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (files != NULL, FALSE);
	g_return_val_if_fail (package->priv->files == NULL, FALSE);

	package->priv->files = zif_string_array_ref (files);
	return TRUE;
}

/**
 * zif_package_set_requires:
 **/
gboolean
zif_package_set_requires (ZifPackage *package, ZifDependArray *requires)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (requires != NULL, FALSE);
	g_return_val_if_fail (package->priv->requires == NULL, FALSE);

	package->priv->requires = zif_depend_array_ref (requires);
	return TRUE;
}

/**
 * zif_package_set_provides:
 **/
gboolean
zif_package_set_provides (ZifPackage *package, ZifDependArray *provides)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (provides != NULL, FALSE);
	g_return_val_if_fail (package->priv->provides == NULL, FALSE);

	package->priv->provides = zif_depend_array_ref (provides);
	return TRUE;
}

/**
 * zif_package_finalize:
 **/
static void
zif_package_finalize (GObject *object)
{
	ZifPackage *package;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_PACKAGE (object));
	package = ZIF_PACKAGE (object);

	pk_package_id_free (package->priv->id);
	g_free (package->priv->id_txt);
	g_free (package->priv->summary);
	g_free (package->priv->description);
	g_free (package->priv->license);
	g_free (package->priv->url);
	g_free (package->priv->category);
	g_free (package->priv->location_href);
	zif_string_array_unref (package->priv->files);
	zif_depend_array_unref (package->priv->requires);
	zif_depend_array_unref (package->priv->provides);
	g_object_unref (package->priv->repos);
	g_object_unref (package->priv->groups);

	G_OBJECT_CLASS (zif_package_parent_class)->finalize (object);
}

/**
 * zif_package_class_init:
 **/
static void
zif_package_class_init (ZifPackageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_package_finalize;
	g_type_class_add_private (klass, sizeof (ZifPackagePrivate));
}

/**
 * zif_package_init:
 **/
static void
zif_package_init (ZifPackage *package)
{
	package->priv = ZIF_PACKAGE_GET_PRIVATE (package);
	package->priv->id = NULL;
	package->priv->id_txt = NULL;
	package->priv->summary = NULL;
	package->priv->description = NULL;
	package->priv->license = NULL;
	package->priv->url = NULL;
	package->priv->category = NULL;
	package->priv->files = NULL;
	package->priv->requires = NULL;
	package->priv->provides = NULL;
	package->priv->location_href = NULL;
	package->priv->installed = FALSE;
	package->priv->group = PK_GROUP_ENUM_UNKNOWN;
	package->priv->size = 0;
	package->priv->repos = zif_repos_new ();
	package->priv->groups = zif_groups_new ();
}

/**
 * zif_package_new:
 * Return value: A new package class instance.
 **/
ZifPackage *
zif_package_new (void)
{
	ZifPackage *package;
	package = g_object_new (ZIF_TYPE_PACKAGE, NULL);
	return ZIF_PACKAGE (package);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_package_test (EggTest *test)
{
	ZifPackage *package;

	if (!egg_test_start (test, "ZifPackage"))
		return;

	/************************************************************/
	egg_test_title (test, "get package");
	package = zif_package_new ();
	egg_test_assert (test, package != NULL);

	g_object_unref (package);

	egg_test_end (test);
}
#endif

