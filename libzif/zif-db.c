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

#include "zif-db.h"
#include "zif-config.h"
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
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	if (db->priv->root == NULL) {
		g_set_error_literal (error,
				     ZIF_DB_ERROR,
				     ZIF_DB_ERROR_FAILED,
				     "db not loaded");
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
		g_set_error_literal (error, ZIF_DB_ERROR, ZIF_DB_ERROR_FAILED,
				     "db not loaded");
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

