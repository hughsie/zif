/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-db
 * @short_description: An extra 'database' to store details about packages
 *
 * #ZifDb is a simple flat file 'database' for stroring details about
 * installed packages, such as the command line that installed them,
 * the uid of the user performing the action and the repository they
 * came from.
 *
 * A yumdb is not really a database at all, and is really slow to read
 * and especially slow to write data for packages. It is provided for
 * compatibility with existing users of yum, but long term this
 * functionality should either be folded into rpm itself, or just put
 * into an actual database format like sqlite.
 *
 * Using the filesystem as a database probably wasn't a great design
 * decision.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-config.h"
#include "zif-db.h"
#include "zif-object-array.h"
#include "zif-package-remote.h"

#define ZIF_DB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_DB, ZifDbPrivate))

struct _ZifDbPrivate
{
	gchar			*root;
	ZifConfig		*config;
	guint			 monitor_changed_id;
};

G_DEFINE_TYPE (ZifDb, zif_db, G_TYPE_OBJECT)
static gpointer zif_db_object = NULL;

/**
 * zif_db_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_db_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_db_error");
	return quark;
}

/**
 * zif_db_set_root:
 * @db: A #ZifDb
 * @root: A system wide db root, e.g. "/var/lib/yum/yumdb", or %NULL to use the default.
 * @error: A #GError, or %NULL
 *
 * Sets the path to use as the system wide db directory.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_db_set_root (ZifDb *db, const gchar *root, GError **error)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (ZIF_IS_DB (db), FALSE);
	g_return_val_if_fail (db->priv->root == NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get from config if not specified */
	if (root == NULL) {
		db->priv->root = zif_config_get_string (db->priv->config,
							"yumdb", error);
		if (db->priv->root == NULL)
			goto out;
	} else {
		db->priv->root = g_strdup (root);
	}

	/* check file exists */
	ret = g_file_test (db->priv->root, G_FILE_TEST_IS_DIR);
	if (!ret) {
		g_set_error (error,
			     ZIF_DB_ERROR,
			     ZIF_DB_ERROR_FAILED,
			     "db root %s does not exist",
			     db->priv->root);
		goto out;
	}
out:
	return ret;
}

/**
 * zif_db_create_dir:
 **/
static gboolean
zif_db_create_dir (const gchar *dir, GError **error)
{
	GFile *file = NULL;
	gboolean ret = TRUE;

	/* already exists */
	ret = g_file_test (dir, G_FILE_TEST_IS_DIR);
	if (ret)
		goto out;

	/* need to create */
	g_debug ("creating %s", dir);
	file = g_file_new_for_path (dir);
	ret = g_file_make_directory_with_parents (file, NULL, error);
out:
	if (file != NULL)
		g_object_unref (file);
	return ret;
}

/**
 * zif_db_get_dir_for_package:
 **/
static gchar *
zif_db_get_dir_for_package (ZifDb *db, ZifPackage *package)
{
	gchar *dir;
	dir = g_strdup_printf ("%s/%c/%s-%s-%s-%s",
			       db->priv->root,
			       zif_package_get_name (package)[0],
			       zif_package_get_pkgid (package),
			       zif_package_get_name (package),
			       zif_package_get_version (package),
			       zif_package_get_arch (package));
	return dir;
}

/**
 * zif_db_get_string:
 * @db: A #ZifDb
 * @package: A package to use as a reference
 * @key: A key name to retrieve, e.g. "releasever"
 * @error: A #GError, or %NULL
 *
 * Gets a string value from the yumdb 'database'.
 *
 * Return value: An allocated value, or %NULL
 *
 * Since: 0.1.3
 **/
gchar *
zif_db_get_string (ZifDb *db, ZifPackage *package, const gchar *key, GError **error)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *index_dir = NULL;
	gchar *value = NULL;

	g_return_val_if_fail (ZIF_IS_DB (db), NULL);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	if (db->priv->root == NULL) {
		ret = zif_db_set_root (db, NULL, error);
		if (!ret)
			goto out;
	}

	/* get file contents */
	index_dir = zif_db_get_dir_for_package (db, package);
	filename = g_build_filename (index_dir, key, NULL);

	/* check it exists */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		g_set_error (error,
			     ZIF_DB_ERROR,
			     ZIF_DB_ERROR_FAILED,
			     "%s key not found",
			     filename);
		goto out;
	}

	/* get value */
	ret = g_file_get_contents (filename, &value, NULL, error);
	if (!ret)
		goto out;
out:
	g_free (index_dir);
	g_free (filename);
	return value;
}

/**
 * zif_db_get_keys:
 * @db: A #ZifDb
 * @package: A package to use as a reference
 * @error: A #GError, or %NULL
 *
 * Gets all the keys for a given package.
 *
 * Return value: An allocated value, or %NULL
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_db_get_keys (ZifDb *db, ZifPackage *package, GError **error)
{
	const gchar *filename;
	gboolean ret;
	gchar *index_dir = NULL;
	GDir *dir = NULL;
	GPtrArray *array = NULL;

	g_return_val_if_fail (ZIF_IS_DB (db), NULL);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	if (db->priv->root == NULL) {
		ret = zif_db_set_root (db, NULL, error);
		if (!ret)
			goto out;
	}

	/* get file contents */
	index_dir = zif_db_get_dir_for_package (db, package);

	/* search directory */
	dir = g_dir_open (index_dir, 0, error);
	if (dir == NULL)
		goto out;

	/* return the keys */
	array = g_ptr_array_new_with_free_func (g_free);
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		g_ptr_array_add (array, g_strdup (filename));
		filename = g_dir_read_name (dir);
	}
out:
	if (dir != NULL)
		g_dir_close (dir);
	g_free (index_dir);
	return array;
}

/**
 * zif_db_set_string:
 * @db: A #ZifDb
 * @package: A package to use as a reference
 * @key: Key name to save, e.g. "reason"
 * @value: Key data to save, e.g. "dep"
 * @error: A #GError, or %NULL
 *
 * Writes a data value to the yumdb 'database'.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_db_set_string (ZifDb *db, ZifPackage *package, const gchar *key, const gchar *value, GError **error)
{
	gboolean ret = TRUE;
	gchar *index_dir = NULL;
	gchar *index_file = NULL;

	g_return_val_if_fail (ZIF_IS_DB (db), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not loaded yet */
	if (db->priv->root == NULL) {
		ret = zif_db_set_root (db, NULL, error);
		if (!ret)
			goto out;
	}

	/* create the index directory */
	index_dir = zif_db_get_dir_for_package (db, package);
	ret = zif_db_create_dir (index_dir, error);
	if (!ret)
		goto out;

	/* write the value */
	index_file = g_build_filename (index_dir, key, NULL);
	g_debug ("writing %s to %s", value, index_file);
	ret = g_file_set_contents (index_file, value, -1, error);
	if (!ret)
		goto out;
out:
	g_free (index_dir);
	g_free (index_file);
	return ret;
}

/**
 * zif_db_get_packages_for_filename:
 **/
static gboolean
zif_db_get_packages_for_filename (ZifDb *db,
				  GPtrArray *array,
				  const gchar *filename,
				  GError **error)
{
	gboolean ret = TRUE;
	gchar *package_id = NULL;
	gchar **split = NULL;
	GString *name = NULL;
	GString *version = NULL;
	guint i;
	guint len;
	ZifPackage *package = NULL;
	ZifString *pkgid = NULL;

	/* cut up using a metric. I wish this was a database... */
	split = g_strsplit (filename, "-", -1);
	len = g_strv_length (split);
	if (len < 3)
		goto out;

	/* join up name */
	name = g_string_new ("");
	for (i=1; i<len-3; i++)
		g_string_append_printf (name, "%s-", split[i]);
	g_string_set_size (name, name->len - 1);

	/* join up version */
	version = g_string_new ("");
	for (i=len-3; i<len-1; i++)
		g_string_append_printf (version, "%s-", split[i]);
	g_string_set_size (version, version->len - 1);

	/* create package-id */
	package_id = g_strdup_printf ("%s;%s;%s;%s",
				      name->str,
				      version->str,
				      split[len-1],
				      "installed");

	/* assign package-id */
	package = zif_package_new ();
	ret = zif_package_set_id (package, package_id, error);
	if (!ret)
		goto out;

	/* set pkgid */
	pkgid = zif_string_new (split[0]);
	zif_package_set_pkgid (package, pkgid);
	zif_object_array_add (array, package);
out:
	g_strfreev (split);
	g_free (package_id);
	if (package != NULL)
		g_object_unref (package);
	if (name != NULL)
		g_string_free (name, TRUE);
	if (version != NULL)
		g_string_free (version, TRUE);
	if (pkgid != NULL)
		zif_string_unref (pkgid);
	return ret;
}

/**
 * zif_db_get_packages_for_index:
 **/
static gboolean
zif_db_get_packages_for_index (ZifDb *db,
			       GPtrArray *array,
			       const gchar *path,
			       GError **error)
{
	const gchar *filename;
	gboolean ret = TRUE;
	GDir *dir = NULL;

	/* search directory */
	dir = g_dir_open (path, 0, error);
	if (dir == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get the initial index */
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		ret = zif_db_get_packages_for_filename (db,
							array,
							filename,
							error);
		if (!ret)
			goto out;
		filename = g_dir_read_name (dir);
	}
out:
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * zif_db_get_packages:
 * @db: A #ZifDb
 * @error: A #GError, or %NULL
 *
 * Gets all the packages in the yumdb 'database'.
 *
 * Return value: An array of #ZifPackage's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_db_get_packages (ZifDb *db, GError **error)
{
	const gchar *filename;
	gboolean ret;
	gchar *path;
	GDir *dir = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;

	g_return_val_if_fail (ZIF_IS_DB (db), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	if (db->priv->root == NULL) {
		ret = zif_db_set_root (db, NULL, error);
		if (!ret)
			goto out;
	}

	/* search directory */
	dir = g_dir_open (db->priv->root, 0, error);
	if (dir == NULL)
		goto out;

	/* create an output array */
	array_tmp = zif_object_array_new ();

	/* get the initial index */
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		path = g_build_filename (db->priv->root,
					 filename,
					 NULL);
		if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
			ret = zif_db_get_packages_for_index (db,
							     array_tmp,
							     path,
							     error);
			g_free (path);
			if (!ret)
				goto out;
		} else {
			g_free (path);
		}
		filename = g_dir_read_name (dir);
	}

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	if (dir != NULL)
		g_dir_close (dir);
	return array;
}

/**
 * zif_db_remove:
 * @db: A #ZifDb
 * @package: A package to use as a reference
 * @key: Key name to delete, e.g. "reason"
 * @error: A #GError, or %NULL
 *
 * Removes a data value from the yumdb 'database' for a given package.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_db_remove (ZifDb *db, ZifPackage *package,
	       const gchar *key, GError **error)
{
	gboolean ret = TRUE;
	gchar *index_dir = NULL;
	gchar *index_file = NULL;
	GFile *file = NULL;

	g_return_val_if_fail (ZIF_IS_DB (db), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not loaded yet */
	if (db->priv->root == NULL) {
		ret = zif_db_set_root (db, NULL, error);
		if (!ret)
			goto out;
	}

	/* create the index directory */
	index_dir = zif_db_get_dir_for_package (db, package);

	/* delete the value */
	g_debug ("deleting %s from %s", key, index_dir);
	index_file = g_build_filename (index_dir, key, NULL);
	file = g_file_new_for_path (index_file);
	ret = g_file_delete (file, NULL, error);
	if (!ret)
		goto out;
out:
	if (file != NULL)
		g_object_unref (file);
	g_free (index_dir);
	return ret;
}

/**
 * zif_db_remove_all:
 * @db: A #ZifDb
 * @package: A package to use as a reference
 * @error: A #GError, or %NULL
 *
 * Removes a all data value from the yumdb 'database' for a given package.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_db_remove_all (ZifDb *db, ZifPackage *package, GError **error)
{
	gboolean ret = TRUE;
	gchar *index_dir = NULL;
	gchar *index_file = NULL;
	GFile *file_tmp;
	GFile *file_directory = NULL;
	GDir *dir = NULL;
	const gchar *filename;

	g_return_val_if_fail (ZIF_IS_DB (db), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not loaded yet */
	if (db->priv->root == NULL) {
		ret = zif_db_set_root (db, NULL, error);
		if (!ret)
			goto out;
	}

	/* get the folder */
	index_dir = zif_db_get_dir_for_package (db, package);
	ret = g_file_test (index_dir, G_FILE_TEST_IS_DIR);
	if (!ret) {
		g_debug ("Nothing to delete in %s", index_dir);
		ret = TRUE;
		goto out;
	}

	/* open */
	dir = g_dir_open (index_dir, 0, error);
	if (dir == NULL)
		goto out;

	/* delete each one */
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		index_file = g_build_filename (index_dir, filename, NULL);
		file_tmp = g_file_new_for_path (index_file);

		/* delete, ignoring error */
		g_debug ("deleting %s from %s", filename, index_dir);
		ret = g_file_delete (file_tmp, NULL, NULL);
		if (!ret)
			g_debug ("failed to delete %s", filename);
		g_object_unref (file_tmp);
		g_free (index_file);
		filename = g_dir_read_name (dir);
	}

	/* now delete the directory */
	file_directory = g_file_new_for_path (index_dir);
	ret = g_file_delete (file_directory, NULL, error);
	if (!ret)
		goto out;
out:
	if (file_directory != NULL)
		g_object_unref (file_directory);
	g_free (index_dir);
	return ret;
}

/**
 * zif_db_finalize:
 **/
static void
zif_db_finalize (GObject *object)
{
	ZifDb *db;
	g_return_if_fail (ZIF_IS_DB (object));
	db = ZIF_DB (object);

	g_free (db->priv->root);
	g_object_unref (db->priv->config);

	G_OBJECT_CLASS (zif_db_parent_class)->finalize (object);
}

/**
 * zif_db_class_init:
 **/
static void
zif_db_class_init (ZifDbClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_db_finalize;
	g_type_class_add_private (klass, sizeof (ZifDbPrivate));
}

/**
 * zif_db_init:
 **/
static void
zif_db_init (ZifDb *db)
{
	db->priv = ZIF_DB_GET_PRIVATE (db);
	db->priv->config = zif_config_new ();
}

/**
 * zif_db_new:
 *
 * Return value: A new #ZifDb instance.
 *
 * Since: 0.1.0
 **/
ZifDb *
zif_db_new (void)
{
	if (zif_db_object != NULL) {
		g_object_ref (zif_db_object);
	} else {
		zif_db_object = g_object_new (ZIF_TYPE_DB, NULL);
		g_object_add_weak_pointer (zif_db_object, &zif_db_object);
	}
	return ZIF_DB (zif_db_object);
}

