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

#include "dum-utils.h"
#include "dum-package.h"
#include "dum-repos.h"
#include "dum-groups.h"
#include "dum-string.h"
#include "dum-string-array.h"
#include "dum-depend-array.h"

#define DUM_PACKAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_PACKAGE, DumPackagePrivate))

struct DumPackagePrivate
{
	DumGroups		*groups;
	DumRepos		*repos;
	PkPackageId		*id;
	gchar			*id_txt;
	DumString		*summary;
	DumString		*description;
	DumString		*license;
	DumString		*url;
	DumString		*category;
	DumString		*location_href;
	PkGroupEnum		 group;
	guint64			 size;
	DumStringArray		*files;
	DumDependArray		*requires;
	DumDependArray		*provides;
	gboolean		 installed;
};

G_DEFINE_TYPE (DumPackage, dum_package, G_TYPE_OBJECT)

/**
 * dum_package_compare:
 **/
gint
dum_package_compare (DumPackage *a, DumPackage *b)
{
	const PkPackageId *ida;
	const PkPackageId *idb;
	gint val = 0;

	g_return_val_if_fail (DUM_IS_PACKAGE (a), 0);
	g_return_val_if_fail (DUM_IS_PACKAGE (b), 0);

	/* shallow copy */
	ida = dum_package_get_id (a);
	idb = dum_package_get_id (b);

	/* check name the same */
	if (g_strcmp0 (ida->name, idb->name) != 0) {
		egg_warning ("comparing between %s and %s", ida->name, idb->name);
		goto out;
	}

	/* do a version compare */
	val = dum_compare_evr (ida->version, idb->version);
out:
	return val;
}

/**
 * dum_package_download:
 **/
gboolean
dum_package_download (DumPackage *package, const gchar *directory, GError **error)
{
	gboolean ret = FALSE;
	DumStoreRemote *repo = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);
	g_return_val_if_fail (package->priv->id != NULL, FALSE);

	/* check we are not installed */
	if (package->priv->installed) {
		if (error != NULL)
			*error = g_error_new (1, 0, "cannot download installed packages");
		goto out;
	}

	/* find correct repo */
	repo = dum_repos_get_store (package->priv->repos, package->priv->id->data, &error_local);
	if (repo == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "cannot find remote repo: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* download from the repo */
	ret = dum_store_remote_download (repo, package->priv->location_href->value, directory, &error_local);
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
 * dum_package_print:
 **/
void
dum_package_print (DumPackage *package)
{
	guint i;
	gchar *text;
	DumDepend *depend;

	g_return_if_fail (DUM_IS_PACKAGE (package));
	g_return_if_fail (package->priv->id != NULL);

	g_print ("id=%s\n", package->priv->id_txt);
	g_print ("summary=%s\n", package->priv->summary->value);
	g_print ("description=%s\n", package->priv->description->value);
	g_print ("license=%s\n", package->priv->license->value);
	g_print ("group=%s\n", pk_group_enum_to_text (package->priv->group));
	g_print ("category=%s\n", package->priv->category->value);
	if (package->priv->url != NULL)
		g_print ("url=%s\n", package->priv->url->value);
	g_print ("size=%"G_GUINT64_FORMAT"\n", package->priv->size);

	if (package->priv->files != NULL) {
		g_print ("files:\n");
		for (i=0; i<package->priv->files->value->len; i++)
			g_print ("\t%s\n", (const gchar *) g_ptr_array_index (package->priv->files->value, i));
	}
	if (package->priv->requires != NULL) {
		g_print ("requires:\n");
		for (i=0; i<package->priv->requires->value->len; i++) {
			depend = g_ptr_array_index (package->priv->requires->value, i);
			text = dum_depend_to_string (depend);
			g_print ("\t%s\n", text);
			g_free (text);
		}
	}
	if (package->priv->provides != NULL) {
		g_print ("provides:\n");
		for (i=0; i<package->priv->provides->value->len; i++) {
			depend = g_ptr_array_index (package->priv->provides->value, i);
			text = dum_depend_to_string (depend);
			g_print ("\t%s\n", text);
			g_free (text);
		}
	}
}

/**
 * dum_package_is_devel:
 **/
gboolean
dum_package_is_devel (DumPackage *package)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
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
 * dum_package_is_gui:
 **/
gboolean
dum_package_is_gui (DumPackage *package)
{
	guint i;
	DumDepend *depend;

	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->id != NULL, FALSE);

	/* trivial */
	if (package->priv->requires == NULL)
		return FALSE;

	for (i=0; i<package->priv->requires->value->len; i++) {
		depend = g_ptr_array_index (package->priv->requires->value, i);
		if (strstr (depend->name, "gtk") != NULL)
			return TRUE;
		if (strstr (depend->name, "kde") != NULL)
			return TRUE;
	}
	return FALSE;
}

/**
 * dum_package_is_installed:
 **/
gboolean
dum_package_is_installed (DumPackage *package)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->id != NULL, FALSE);
	return package->priv->installed;
}

/**
 * dum_package_is_free:
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
dum_package_is_free (DumPackage *package)
{
	gboolean one_free_group = FALSE;
	gboolean group_is_free;
	gchar **groups;
	gchar **licenses;
	guint len;
	guint i;
	guint j;

	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->id != NULL, FALSE);

	/* split AND clase */
	groups = g_strsplit (package->priv->license->value, " and ", 0);
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
 * dum_package_get_id:
 **/
const PkPackageId *
dum_package_get_id (DumPackage *package)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return package->priv->id;
}

/**
 * dum_package_get_package_id:
 **/
const gchar *
dum_package_get_package_id (DumPackage *package)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return package->priv->id_txt;
}

/**
 * dum_package_get_summary:
 **/
DumString *
dum_package_get_summary (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return dum_string_ref (package->priv->summary);
}

/**
 * dum_package_get_description:
 **/
DumString *
dum_package_get_description (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return dum_string_ref (package->priv->description);
}

/**
 * dum_package_get_license:
 **/
DumString *
dum_package_get_license (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return dum_string_ref (package->priv->license);
}

/**
 * dum_package_get_url:
 **/
DumString *
dum_package_get_url (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return dum_string_ref (package->priv->url);
}

/**
 * dum_package_get_filename:
 * e.g. Packages/net-snmp-5.4.2-3.fc10.i386.rpm
 **/
DumString *
dum_package_get_filename (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return dum_string_ref (package->priv->location_href);
}

/**
 * dum_package_get_category:
 **/
DumString *
dum_package_get_category (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return dum_string_ref (package->priv->category);
}

/**
 * dum_package_get_group:
 **/
PkGroupEnum
dum_package_get_group (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (package->priv->id != NULL, PK_GROUP_ENUM_UNKNOWN);
	return package->priv->group;
}

/**
 * dum_package_get_size:
 **/
guint64
dum_package_get_size (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), 0);
	g_return_val_if_fail (package->priv->id != NULL, 0);
	return package->priv->size;
}

/**
 * dum_package_get_files:
 **/
DumStringArray *
dum_package_get_files (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	if (package->priv->files == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "no data for %s", package->priv->id->name);
	}
	return package->priv->files;
}

/**
 * dum_package_get_requires:
 **/
DumDependArray *
dum_package_get_requires (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return package->priv->requires;
}

/**
 * dum_package_get_provides:
 **/
DumDependArray *
dum_package_get_provides (DumPackage *package, GError **error)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->id != NULL, NULL);
	return package->priv->provides;
}

/**
 * dum_package_set_installed:
 **/
gboolean
dum_package_set_installed (DumPackage *package, gboolean installed)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	package->priv->installed = installed;
	return TRUE;
}

/**
 * dum_package_set_id:
 **/
gboolean
dum_package_set_id (DumPackage *package, const PkPackageId *id)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (package->priv->id == NULL, FALSE);

	package->priv->id = pk_package_id_copy (id);
	package->priv->id_txt = pk_package_id_to_string (package->priv->id);
	return TRUE;
}

/**
 * dum_package_set_summary:
 **/
gboolean
dum_package_set_summary (DumPackage *package, DumString *summary)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (summary != NULL, FALSE);
	g_return_val_if_fail (package->priv->summary == NULL, FALSE);

	package->priv->summary = dum_string_ref (summary);
	return TRUE;
}

/**
 * dum_package_set_description:
 **/
gboolean
dum_package_set_description (DumPackage *package, DumString *description)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (description != NULL, FALSE);
	g_return_val_if_fail (package->priv->description == NULL, FALSE);

	package->priv->description = dum_string_ref (description);
	return TRUE;
}

/**
 * dum_package_set_license:
 **/
gboolean
dum_package_set_license (DumPackage *package, DumString *license)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (license != NULL, FALSE);
	g_return_val_if_fail (package->priv->license == NULL, FALSE);

	package->priv->license = dum_string_ref (license);
	return TRUE;
}

/**
 * dum_package_set_url:
 **/
gboolean
dum_package_set_url (DumPackage *package, DumString *url)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (url != NULL, FALSE);
	g_return_val_if_fail (package->priv->url == NULL, FALSE);

	package->priv->url = dum_string_ref (url);
	return TRUE;
}

/**
 * dum_package_set_location_href:
 **/
gboolean
dum_package_set_location_href (DumPackage *package, DumString *location_href)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (location_href != NULL, FALSE);
	g_return_val_if_fail (package->priv->location_href == NULL, FALSE);

	package->priv->location_href = dum_string_ref (location_href);
	return TRUE;
}

/**
 * dum_package_set_category:
 **/
gboolean
dum_package_set_category (DumPackage *package, DumString *category)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (category != NULL, FALSE);
	g_return_val_if_fail (package->priv->category == NULL, FALSE);

	package->priv->category = dum_string_ref (category);
	return TRUE;
}

/**
 * dum_package_set_group:
 **/
gboolean
dum_package_set_group (DumPackage *package, PkGroupEnum group)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (group != PK_GROUP_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (package->priv->group == PK_GROUP_ENUM_UNKNOWN, FALSE);

	package->priv->group = group;
	return TRUE;
}

/**
 * dum_package_set_size:
 **/
gboolean
dum_package_set_size (DumPackage *package, guint64 size)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (size != 0, FALSE);
	g_return_val_if_fail (package->priv->size == 0, FALSE);

	package->priv->size = size;
	return TRUE;
}

/**
 * dum_package_set_files:
 **/
gboolean
dum_package_set_files (DumPackage *package, DumStringArray *files)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (files != NULL, FALSE);
	g_return_val_if_fail (package->priv->files == NULL, FALSE);

	package->priv->files = dum_string_array_ref (files);
	return TRUE;
}

/**
 * dum_package_set_requires:
 **/
gboolean
dum_package_set_requires (DumPackage *package, DumDependArray *requires)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (requires != NULL, FALSE);
	g_return_val_if_fail (package->priv->requires == NULL, FALSE);

	package->priv->requires = dum_depend_array_ref (requires);
	return TRUE;
}

/**
 * dum_package_set_provides:
 **/
gboolean
dum_package_set_provides (DumPackage *package, DumDependArray *provides)
{
	g_return_val_if_fail (DUM_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (provides != NULL, FALSE);
	g_return_val_if_fail (package->priv->provides == NULL, FALSE);

	package->priv->provides = dum_depend_array_ref (provides);
	return TRUE;
}

/**
 * dum_package_finalize:
 **/
static void
dum_package_finalize (GObject *object)
{
	DumPackage *package;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_PACKAGE (object));
	package = DUM_PACKAGE (object);

	pk_package_id_free (package->priv->id);
	g_free (package->priv->id_txt);
	g_free (package->priv->summary);
	g_free (package->priv->description);
	g_free (package->priv->license);
	g_free (package->priv->url);
	g_free (package->priv->category);
	g_free (package->priv->location_href);
	dum_string_array_unref (package->priv->files);
	dum_depend_array_unref (package->priv->requires);
	dum_depend_array_unref (package->priv->provides);
	g_object_unref (package->priv->repos);
	g_object_unref (package->priv->groups);

	G_OBJECT_CLASS (dum_package_parent_class)->finalize (object);
}

/**
 * dum_package_class_init:
 **/
static void
dum_package_class_init (DumPackageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_package_finalize;
	g_type_class_add_private (klass, sizeof (DumPackagePrivate));
}

/**
 * dum_package_init:
 **/
static void
dum_package_init (DumPackage *package)
{
	package->priv = DUM_PACKAGE_GET_PRIVATE (package);
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
	package->priv->repos = dum_repos_new ();
	package->priv->groups = dum_groups_new ();
}

/**
 * dum_package_new:
 * Return value: A new package class instance.
 **/
DumPackage *
dum_package_new (void)
{
	DumPackage *package;
	package = g_object_new (DUM_TYPE_PACKAGE, NULL);
	return DUM_PACKAGE (package);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_package_test (EggTest *test)
{
	DumPackage *package;

	if (!egg_test_start (test, "DumPackage"))
		return;

	/************************************************************/
	egg_test_title (test, "get package");
	package = dum_package_new ();
	egg_test_assert (test, package != NULL);

	g_object_unref (package);

	egg_test_end (test);
}
#endif

