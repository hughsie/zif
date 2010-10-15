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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:zif-config
 * @short_description: A #ZifConfig object manages system wide config options
 *
 * #ZifConfig allows settings to be read from a central config file. Some
 * values can be overridden in a running instance.
 *
 * The values that are overridden can be reset back to the defaults without
 * re-reading the config file.
 *
 * Different types of data can be read (string, bool, uint, time).
 * Before reading any data, the backing config file has to be set with
 * zif_config_set_filename() and any reads prior to that will fail.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <rpm/rpmlib.h>

#include "zif-config.h"
#include "zif-utils.h"
#include "zif-monitor.h"

#define ZIF_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_CONFIG, ZifConfigPrivate))

struct _ZifConfigPrivate
{
	GKeyFile		*keyfile;
	gboolean		 loaded;
	ZifMonitor		*monitor;
	guint			 monitor_changed_id;
	GHashTable		*hash;
	gchar			**basearch_list;
};

G_DEFINE_TYPE (ZifConfig, zif_config, G_TYPE_OBJECT)
static gpointer zif_config_object = NULL;

/**
 * zif_config_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_config_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_config_error");
	return quark;
}

/**
 * zif_config_get_string:
 * @config: the #ZifConfig object
 * @key: the key name to retrieve, e.g. "cachedir"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets a string value from a local setting, falling back to the config file.
 *
 * Return value: the allocated value, or %NULL
 *
 * Since: 0.1.0
 **/
gchar *
zif_config_get_string (ZifConfig *config, const gchar *key, GError **error)
{
	gchar *value = NULL;
	const gchar *value_tmp;
	const gchar *info;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	if (!config->priv->loaded) {
		g_set_error_literal (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
				     "config not loaded");
		goto out;
	}

	/* exists as local override */
	value_tmp = g_hash_table_lookup (config->priv->hash, key);
	if (value_tmp != NULL) {
		value = g_strdup (value_tmp);
		goto out;
	}

	/* get value */
	value = g_key_file_get_string (config->priv->keyfile, "main", key, &error_local);
	if (value != NULL)
		goto out;

	/* special keys, FIXME: add to yum */
	if (g_strcmp0 (key, "reposdir") == 0) {
		value = g_strdup ("/etc/yum.repos.d");
		goto free_error;
	}
	if (g_strcmp0 (key, "pidfile") == 0) {
		value = g_strdup ("/var/run/yum.pid");
		goto free_error;
	}

	/* special rpmkeys */
	if (g_strcmp0 (key, "osinfo") == 0) {
		rpmGetOsInfo (&info, NULL);
		value = g_strdup (info);
		goto free_error;
	}
	if (g_strcmp0 (key, "archinfo") == 0) {
		rpmGetArchInfo (&info, NULL);
		value = g_strdup (info);
		goto free_error;
	}

	/* dumb metadata */
	if (g_strcmp0 (key, "basearch") == 0) {
		rpmGetArchInfo (&info, NULL);
		if (g_strcmp0 (info, "i486") == 0 ||
		    g_strcmp0 (info, "i586") == 0 ||
		    g_strcmp0 (info, "i686") == 0)
			info = "i386";
		value = g_strdup (info);
		goto free_error;
	}

	/* nothing matched */
	g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
		     "failed to read %s: %s", key, error_local->message);
free_error:
	g_error_free (error_local);
out:
	return value;
}

/**
 * zif_config_get_boolean:
 * @config: the #ZifConfig object
 * @key: the key name to retrieve, e.g. "keepcache"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets a boolean value from a local setting, falling back to the config file.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.1.0
 **/
gboolean
zif_config_get_boolean (ZifConfig *config, const gchar *key, GError **error)
{
	gchar *value;
	gboolean ret = FALSE;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get string value */
	value = zif_config_get_string (config, key, error);
	if (value == NULL)
		goto out;

	/* convert to bool */
	ret = zif_boolean_from_text (value);

out:
	g_free (value);
	return ret;
}

/**
 * zif_config_get_uint:
 * @config: the #ZifConfig object
 * @key: the key name to retrieve, e.g. "keepcache"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets a unsigned integer value from a local setting, falling back to the config file.
 *
 * Return value: the data value, or %G_MAXUINT for error
 *
 * Since: 0.1.0
 **/
guint
zif_config_get_uint (ZifConfig *config, const gchar *key, GError **error)
{
	gchar *value;
	guint retval = G_MAXUINT;
	gchar *endptr = NULL;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), G_MAXUINT);
	g_return_val_if_fail (key != NULL, G_MAXUINT);
	g_return_val_if_fail (error == NULL || *error == NULL, G_MAXUINT);

	/* get string value */
	value = zif_config_get_string (config, key, error);
	if (value == NULL)
		goto out;

	/* convert to int */
	retval = g_ascii_strtoull (value, &endptr, 10);
	if (value == endptr) {
		g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
			     "failed to convert '%s' to unsigned integer", value);
		goto out;
	}

out:
	g_free (value);
	return retval;
}

/**
 * zif_config_get_time:
 * @config: the #ZifConfig object
 * @key: the key name to retrieve, e.g. "metadata_expire"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets a time value from a local setting, falling back to the config file.
 *
 * Return value: the data value, or 0 for an error
 *
 * Since: 0.1.0
 **/
guint
zif_config_get_time (ZifConfig *config, const gchar *key, GError **error)
{
	gchar *value;
	guint timeval = 0;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), 0);
	g_return_val_if_fail (key != NULL, 0);
	g_return_val_if_fail (error == NULL || *error == NULL, 0);

	/* get string value */
	value = zif_config_get_string (config, key, error);
	if (value == NULL)
		goto out;

	/* convert to time */
	timeval = zif_time_string_to_seconds (value);
out:
	g_free (value);
	return timeval;
}

/**
 * zif_config_strreplace:
 **/
static gchar *
zif_config_strreplace (const gchar *text, const gchar *find, const gchar *replace)
{
	gchar **array;
	gchar *retval;

	/* common case, not found */
	if (g_strstr_len (text, -1, find) == NULL) {
		return g_strdup (text);
	}

	/* split apart and rejoin with new delimiter */
	array = g_strsplit (text, find, 0);
	retval = g_strjoinv (replace, array);
	g_strfreev (array);
	return retval;
}

/**
 * zif_config_expand_substitutions:
 * @config: the #ZifConfig object
 * @text: string to scan, e.g. "http://fedora/$releasever/$basearch/moo.rpm"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Replaces substitutions in text with the actual values of the running system.
 *
 * Return value: A new allocated string or %NULL for error, free with g_free()
 *
 * Since: 0.1.0
 **/
gchar *
zif_config_expand_substitutions (ZifConfig *config, const gchar *text, GError **error)
{
	gchar *basearch = NULL;
	gchar *releasever = NULL;
	gchar *name1 = NULL;
	gchar *name2 = NULL;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), NULL);
	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get data */
	basearch = zif_config_get_string (config, "basearch", error);
	if (basearch == NULL)
		goto out;

	releasever = zif_config_get_string (config, "releasever", error);
	if (releasever == NULL)
		goto out;

	/* do the replacements */
	name1 = zif_config_strreplace (text, "$releasever", releasever);
	name2 = zif_config_strreplace (name1, "$basearch", basearch);

out:
	g_free (basearch);
	g_free (releasever);
	g_free (name1);
	return name2;
}

/**
 * zif_config_get_basearch_array:
 * @config: the #ZifConfig object
 *
 * Gets the list of architectures that packages are native on for this machine.
 *
 * Return value: A array of strings, do not free, e.g. [ "i386", "i486", "noarch" ]
 *
 * Since: 0.1.0
 **/
gchar **
zif_config_get_basearch_array (ZifConfig *config)
{
	g_return_val_if_fail (ZIF_IS_CONFIG (config), NULL);
	return config->priv->basearch_list;
}

/**
 * zif_config_get_release_filename:
 **/
static const gchar *
zif_config_get_release_filename (ZifConfig *config)
{
	const gchar *filename;
	filename = "/etc/fedora-release";
	if (g_file_test (filename, G_FILE_TEST_EXISTS))
		return filename;
	filename = "/etc/redhat-release";
	if (g_file_test (filename, G_FILE_TEST_EXISTS))
		return filename;
	return NULL;
}

/**
 * zif_config_set_filename:
 * @config: the #ZifConfig object
 * @filename: the system wide config file, e.g. "/etc/yum.conf"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets the filename to use as the system wide config file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_config_set_filename (ZifConfig *config, const gchar *filename, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *basearch = NULL;
	gchar *releasever = NULL;
	const gchar *release_filename;
	const gchar *text;
	GPtrArray *array;
	guint i;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (!config->priv->loaded, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check file exists */
	ret = g_file_test (filename, G_FILE_TEST_IS_REGULAR);
	if (!ret) {
		g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
			     "config file %s does not exist", filename);
		goto out;
	}

	/* setup watch */
	ret = zif_monitor_add_watch (config->priv->monitor, filename, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
			     "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* load file */
	ret = g_key_file_load_from_file (config->priv->keyfile, filename, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
			     "failed to load config file: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* done */
	config->priv->loaded = TRUE;

	/* calculate the release version if not specified in the config file */
	releasever = zif_config_get_string (config, "releasever", NULL);
	if (releasever == NULL) {

		/* get correct file */
		release_filename = zif_config_get_release_filename (config);
		if (release_filename == NULL) {
			ret = FALSE;
			g_set_error_literal (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
					     "could not get a correct release filename");
			goto out;
		}

		/* get distro constants from fedora-release */
		ret = g_file_get_contents (release_filename, &releasever, NULL, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
				     "failed to get distro release version: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* get the value from 'Fedora release 11.92 (Rawhide)' */
		g_strdelimit (releasever, " ", '\0');

		/* set local */
		ret = zif_config_set_local (config, "releasever", releasever+15, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
				     "failed to set distro release version: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* calculate the valid basearchs */
	basearch = zif_config_get_string (config, "basearch", &error_local);
	if (basearch == NULL) {
		g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
			     "failed to get basearch: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* add valid archs to array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	g_ptr_array_add (array, g_strdup (basearch));
	g_ptr_array_add (array, g_strdup ("noarch"));
	if (g_strcmp0 (basearch, "i386") == 0) {
		g_ptr_array_add (array, g_strdup ("i486"));
		g_ptr_array_add (array, g_strdup ("i586"));
		g_ptr_array_add (array, g_strdup ("i686"));
	}

	/* copy into GStrv array */
	config->priv->basearch_list = g_new0 (gchar*, array->len+1);
	for (i=0; i < array->len; i++) {
		text = g_ptr_array_index (array, i);
		config->priv->basearch_list[i] = g_strdup (text);
	}
	g_ptr_array_unref (array);
out:
	g_free (basearch);
	g_free (releasever);
	return ret;
}

/**
 * zif_config_reset_default:
 * @config: the #ZifConfig object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Removes any local settings previously set.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_config_reset_default (ZifConfig *config, GError **error)
{
	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_hash_table_remove_all (config->priv->hash);
	return TRUE;
}

/**
 * zif_config_set_local:
 * @config: the #ZifConfig object
 * @key: the key name to save, e.g. "keepcache"
 * @value: the key data to save, e.g. "always"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets a local value which is used in preference to the config value.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_config_set_local (ZifConfig *config, const gchar *key, const gchar *value, GError **error)
{
	const gchar *value_tmp;
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already exists? */
	value_tmp = g_hash_table_lookup (config->priv->hash, key);
	if (value_tmp != NULL) {
		g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
			     "already set key %s to %s, cannot overwrite with %s", key, value_tmp, value);
		ret = FALSE;
		goto out;
	}

	/* insert into table */
	g_hash_table_insert (config->priv->hash, g_strdup (key), g_strdup (value));
out:
	return ret;
}

/**
 * zif_config_file_monitor_cb:
 **/
static void
zif_config_file_monitor_cb (ZifMonitor *monitor, ZifConfig *config)
{
	g_warning ("config file changed");
	config->priv->loaded = FALSE;
}

/**
 * zif_config_finalize:
 **/
static void
zif_config_finalize (GObject *object)
{
	ZifConfig *config;
	g_return_if_fail (ZIF_IS_CONFIG (object));
	config = ZIF_CONFIG (object);

	g_key_file_free (config->priv->keyfile);
	g_hash_table_unref (config->priv->hash);
	g_signal_handler_disconnect (config->priv->monitor,
				     config->priv->monitor_changed_id);
	g_object_unref (config->priv->monitor);
	g_strfreev (config->priv->basearch_list);

	G_OBJECT_CLASS (zif_config_parent_class)->finalize (object);
}

/**
 * zif_config_class_init:
 **/
static void
zif_config_class_init (ZifConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_config_finalize;
	g_type_class_add_private (klass, sizeof (ZifConfigPrivate));
}

/**
 * zif_config_init:
 **/
static void
zif_config_init (ZifConfig *config)
{
	config->priv = ZIF_CONFIG_GET_PRIVATE (config);
	config->priv->keyfile = g_key_file_new ();
	config->priv->loaded = FALSE;
	config->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	config->priv->basearch_list = NULL;
	config->priv->monitor = zif_monitor_new ();
	config->priv->monitor_changed_id =
		g_signal_connect (config->priv->monitor, "changed",
				  G_CALLBACK (zif_config_file_monitor_cb), config);
}

/**
 * zif_config_new:
 *
 * Return value: A new #ZifConfig class instance.
 *
 * Since: 0.1.0
 **/
ZifConfig *
zif_config_new (void)
{
	if (zif_config_object != NULL) {
		g_object_ref (zif_config_object);
	} else {
		zif_config_object = g_object_new (ZIF_TYPE_CONFIG, NULL);
		g_object_add_weak_pointer (zif_config_object, &zif_config_object);
	}
	return ZIF_CONFIG (zif_config_object);
}

