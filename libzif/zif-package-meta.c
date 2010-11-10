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
 * SECTION:zif-package-meta
 * @short_description: Meta package object, populated from a spec file
 *
 * This object is a subclass of #ZifPackage
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "zif-depend.h"
#include "zif-package.h"
#include "zif-package-meta.h"
#include "zif-string.h"
#include "zif-utils.h"

#define ZIF_PACKAGE_META_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_PACKAGE_META, ZifPackageMetaPrivate))

/**
 * ZifPackageMetaPrivate:
 *
 * Private #ZifPackageMeta data
 **/
struct _ZifPackageMetaPrivate
{
	GPtrArray		*array;
};

G_DEFINE_TYPE (ZifPackageMeta, zif_package_meta, ZIF_TYPE_PACKAGE)

/*
 * zif_package_meta_get_string:
 */
static gchar *
zif_package_meta_get_string (ZifPackageMeta *pkg, const gchar *key)
{
	guint i;
	guint keylen;
	gchar *value = NULL;
	const gchar *data;

	/* find a single string */
	keylen = strlen (key);
	for (i=0; i<pkg->priv->array->len; i++) {
		data = g_ptr_array_index (pkg->priv->array, i);
		if (g_str_has_prefix (data, key)) {
			if (data[keylen + 1] == ' ')
				keylen++;
			value = g_strdup (data + keylen + 1);
			break;
		}
	}
	return value;
}

/*
 * zif_package_meta_get_depends:
 */
static GPtrArray *
zif_package_meta_get_depends (ZifPackageMeta *pkg, const gchar *key)
{
	guint i;
	guint keylen;
	gboolean ret;
	GPtrArray *value;
	const gchar *data;
	ZifDepend *depend;
	GError *error = NULL;

	/* find an array of ZifDepends */
	keylen = strlen (key);
	value = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<pkg->priv->array->len; i++) {
		data = g_ptr_array_index (pkg->priv->array, i);
		if (g_str_has_prefix (data, key)) {
			if (data[keylen + 1] == ' ')
				keylen++;
			depend = zif_depend_new ();
			ret = zif_depend_parse_description (depend, data + keylen + 1, &error);
			if (ret) {
				g_ptr_array_add (value, g_object_ref (depend));
			} else {
				g_debug ("failed to parse '%s': %s", data, error->message);
				g_clear_error (&error);
			}
			g_object_unref (depend);
		}
	}


	return value;
}

/*
 * zif_package_meta_ensure_data:
 */
static gboolean
zif_package_meta_ensure_data (ZifPackage *pkg, ZifPackageEnsureType type, ZifState *state, GError **error)
{
	ZifString *tmp;
	GPtrArray *depends;
	gboolean ret = TRUE;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	if (type == ZIF_PACKAGE_ENSURE_TYPE_SUMMARY) {
		tmp = zif_string_new_value (zif_package_meta_get_string (ZIF_PACKAGE_META(pkg), "Summary"));
		zif_package_set_summary (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_LICENCE) {
		tmp = zif_string_new_value (zif_package_meta_get_string (ZIF_PACKAGE_META(pkg), "License"));
		zif_package_set_license (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_URL) {
		tmp = zif_string_new_value (zif_package_meta_get_string (ZIF_PACKAGE_META(pkg), "URL"));
		if (tmp != NULL) {
			zif_package_set_url (pkg, tmp);
			zif_string_unref (tmp);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {
		depends = zif_package_meta_get_depends (ZIF_PACKAGE_META(pkg), "Requires");
		zif_package_set_requires (pkg, depends);
		g_ptr_array_unref (depends);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {
		depends = zif_package_meta_get_depends (ZIF_PACKAGE_META(pkg), "Provides");
		zif_package_set_provides (pkg, depends);
		g_ptr_array_unref (depends);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {
		depends = zif_package_meta_get_depends (ZIF_PACKAGE_META(pkg), "Conflicts");
		zif_package_set_conflicts (pkg, depends);
		g_ptr_array_unref (depends);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {
		depends = zif_package_meta_get_depends (ZIF_PACKAGE_META(pkg), "Obsoletes");
		zif_package_set_obsoletes (pkg, depends);
		g_ptr_array_unref (depends);
	} else {
		ret = FALSE;
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "failed to get ensure data %s",
			     zif_package_ensure_type_to_string (type));
	}
	return ret;
}

/**
 * zif_package_meta_set_from_filename:
 * @pkg: the #ZifPackageMeta object
 * @filename: the meta filename
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets a meta package object from a meta file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_meta_set_from_filename (ZifPackageMeta *pkg, const gchar *filename, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar **lines = NULL;
	gchar *data = NULL;
	guint i;
	gchar *name = NULL;
	gchar *version = NULL;
	gchar *release = NULL;
	gchar *package_id = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE_META (pkg), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* open file */
	ret = g_file_get_contents (filename, &data, NULL, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "failed to set from header: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* parse wach line */
	lines = g_strsplit (data, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		if (g_strstr_len (lines[i], -1, ": ") != NULL)
			g_ptr_array_add (pkg->priv->array, g_strdup (lines[i]));
	}

	/* get core data */
	name = zif_package_meta_get_string (pkg, "Name");
	version = zif_package_meta_get_string (pkg, "Version");
	release = zif_package_meta_get_string (pkg, "Release");
	//TODO: get epoch and arch
	package_id = zif_package_id_from_nevra (name, 0, version, release, "i386", "meta");

	/* save id */
	ret = zif_package_set_id (ZIF_PACKAGE (pkg), package_id, error);
	if (!ret)
		goto out;
out:
	g_strfreev (lines);
	g_free (data);
	g_free (package_id);
	g_free (name);
	g_free (version);
	g_free (release);
	return ret;
}

/**
 * zif_package_meta_finalize:
 **/
static void
zif_package_meta_finalize (GObject *object)
{
	ZifPackageMeta *pkg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_PACKAGE_META (object));
	pkg = ZIF_PACKAGE_META (object);

	g_ptr_array_unref (pkg->priv->array);

	G_OBJECT_CLASS (zif_package_meta_parent_class)->finalize (object);
}

/**
 * zif_package_meta_class_init:
 **/
static void
zif_package_meta_class_init (ZifPackageMetaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifPackageClass *package_class = ZIF_PACKAGE_CLASS (klass);
	object_class->finalize = zif_package_meta_finalize;

	package_class->ensure_data = zif_package_meta_ensure_data;

	g_type_class_add_private (klass, sizeof (ZifPackageMetaPrivate));
}

/**
 * zif_package_meta_init:
 **/
static void
zif_package_meta_init (ZifPackageMeta *pkg)
{
	pkg->priv = ZIF_PACKAGE_META_GET_PRIVATE (pkg);
	pkg->priv->array = g_ptr_array_new_with_free_func (g_free);
}

/**
 * zif_package_meta_new:
 *
 * Return value: A new #ZifPackageMeta class instance.
 *
 * Since: 0.1.3
 **/
ZifPackage *
zif_package_meta_new (void)
{
	ZifPackage *pkg;
	pkg = g_object_new (ZIF_TYPE_PACKAGE_META, NULL);
	return ZIF_PACKAGE (pkg);
}

