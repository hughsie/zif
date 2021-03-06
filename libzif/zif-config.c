/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2011 Richard Hughes <richard@hughsie.com>
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
 * @short_description: System wide config options
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
#include "zif-utils-private.h"
#include "zif-monitor.h"

#define ZIF_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_CONFIG, ZifConfigPrivate))

struct _ZifConfigPrivate
{
	gchar			*filename;
	GKeyFile		*file_override;
	GKeyFile		*file_default;
	gboolean		 loaded;
	ZifMonitor		*monitor;
	guint			 monitor_changed_id;
	GHashTable		*hash_override;
	GHashTable		*hash_default;
	gchar			**basearch_list;
	GMutex			 mutex;
};

G_DEFINE_TYPE (ZifConfig, zif_config, G_TYPE_OBJECT)
static gpointer zif_config_object = NULL;

/**
 * zif_config_error_quark:
 *
 * Return value: An error quark.
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
 * zif_config_is_instance_valid:
 *
 * Return value: %TRUE if a singleton instance already exists
 *
 * Since: 0.1.6
 **/
gboolean
zif_config_is_instance_valid (void)
{
	return (zif_config_object != NULL);
}

/**
 * zif_config_load:
 **/
static gboolean
zif_config_load (ZifConfig *config,
		 GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	guint config_schema_version;

	/* lock other threads */
	g_mutex_lock (&config->priv->mutex);

	/* already loaded */
	if (config->priv->loaded)
		goto out;

	/* nothing set */
	if (config->priv->filename == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_CONFIG_ERROR,
				     ZIF_CONFIG_ERROR_FAILED,
				     "no filename set, you need to use "
				     "zif_config_set_filename()!");
		goto out;
	}

	/* load files */
	g_debug ("loading config file %s", config->priv->filename);
	ret = g_key_file_load_from_file (config->priv->file_default,
					 config->priv->filename,
					 G_KEY_FILE_NONE,
					 &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_CONFIG_ERROR,
			     ZIF_CONFIG_ERROR_FAILED,
			     "failed to load config file %s: %s",
			     config->priv->filename,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check schema version key exists */
	config_schema_version = g_key_file_get_integer (config->priv->file_default,
							"main",
							"config_schema_version",
							&error_local);
	if (config_schema_version == 0) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_CONFIG_ERROR,
			     ZIF_CONFIG_ERROR_FAILED,
			     "failed to load config file %s: %s - "
			     "check your installation of Zif",
			     config->priv->filename,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check schema version */
	if (config_schema_version != 1) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_CONFIG_ERROR,
			     ZIF_CONFIG_ERROR_FAILED,
			     "failed to load config file %s - "
			     "check your installation of Zif",
			     config->priv->filename);
		goto out;
	}

	/* done */
	config->priv->loaded = TRUE;
out:
	/* unlock other threads */
	g_mutex_unlock (&config->priv->mutex);
	return ret;
}

/**
 * zif_config_unload:
 **/
static gboolean
zif_config_unload (ZifConfig *config,
		   GError **error)
{
	gboolean ret = TRUE;

	/* already unloaded */
	if (!config->priv->loaded)
		goto out;

	/* done */
	g_debug ("unloading config");
	config->priv->loaded = FALSE;
out:
	return ret;
}

/**
 * zif_config_unset:
 * @config: A #ZifConfig
 * @key: A key name to unset, e.g. "cachedir"
 * @error: A #GError, or %NULL
 *
 * Unsets an overriden value back to the default.
 * Note: if the value was never set then this method also returns
 * with success. The idea is that we unset any value.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.1.3
 **/
gboolean
zif_config_unset (ZifConfig *config, const gchar *key, GError **error)
{
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not loaded yet */
	ret = zif_config_load (config, error);
	if (!ret)
		goto out;

	/* remove */
	g_hash_table_remove (config->priv->hash_override, key);
out:
	return ret;
}

/**
 * zif_config_get_string:
 * @config: A #ZifConfig
 * @key: A key name to retrieve, e.g. "cachedir"
 * @error: A #GError, or %NULL
 *
 * Gets a string value from a local setting, falling back to the config file.
 *
 * Return value: An allocated value, or %NULL
 *
 * Since: 0.1.0
 **/
gchar *
zif_config_get_string (ZifConfig *config,
		       const gchar *key,
		       GError **error)
{
	const gchar *value_tmp;
	gboolean ret;
	gchar *value = NULL;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	ret = zif_config_load (config, error);
	if (!ret)
		goto out;

	/* exists as local override */
	value_tmp = g_hash_table_lookup (config->priv->hash_override, key);
	if (value_tmp != NULL) {
		value = g_strdup (value_tmp);
		goto out;
	}

	/* exists in either config file */
	value = g_key_file_get_string (config->priv->file_override,
				       "main", key, NULL);
	if (value != NULL)
		goto out;
	value = g_key_file_get_string (config->priv->file_default,
				       "main", key, NULL);
	if (value != NULL)
		goto out;

	/* exists as default value */
	value_tmp = g_hash_table_lookup (config->priv->hash_default, key);
	if (value_tmp != NULL) {
		value = g_strdup (value_tmp);
		goto out;
	}

	/* nothing matched */
	g_set_error (error, ZIF_CONFIG_ERROR, ZIF_CONFIG_ERROR_FAILED,
		     "failed to get value for %s", key);
out:
	return value;
}

/**
 * zif_config_get_boolean:
 * @config: A #ZifConfig
 * @key: A key name to retrieve, e.g. "keepcache"
 * @error: A #GError, or %NULL
 *
 * Gets a boolean value from a local setting, falling back to the config file.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.1.0
 **/
gboolean
zif_config_get_boolean (ZifConfig *config,
			const gchar *key,
			GError **error)
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
 * zif_config_get_strv:
 * @config: A #ZifConfig
 * @key: A key name to retrieve, e.g. "keepcache"
 * @error: A #GError, or %NULL
 *
 * Gets a string array value from a local setting, falling back to the config file.
 *
 * Return value: (element-type utf8) (transfer full): %NULL, or a string array
 *
 * Since: 0.1.3
 **/
gchar **
zif_config_get_strv (ZifConfig *config,
		     const gchar *key,
		     GError **error)
{
	gchar *value;
	gchar **split = NULL;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get string value */
	value = zif_config_get_string (config, key, error);
	if (value == NULL)
		goto out;

	/* convert to array */
	split = g_strsplit (value, ",", -1);
out:
	g_free (value);
	return split;
}

/**
 * zif_config_get_uint:
 * @config: A #ZifConfig
 * @key: A key name to retrieve, e.g. "keepcache"
 * @error: A #GError, or %NULL
 *
 * Gets a unsigned integer value from a local setting, falling back to the config file.
 *
 * Return value: Data value, or %G_MAXUINT for error
 *
 * Since: 0.1.0
 **/
guint
zif_config_get_uint (ZifConfig *config,
		     const gchar *key,
		     GError **error)
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
		g_set_error (error,
			     ZIF_CONFIG_ERROR,
			     ZIF_CONFIG_ERROR_FAILED,
			     "failed to convert '%s' to unsigned integer", value);
		goto out;
	}

out:
	g_free (value);
	return retval;
}

/**
 * zif_config_get_time:
 * @config: A #ZifConfig
 * @key: A key name to retrieve, e.g. "metadata_expire"
 * @error: A #GError, or %NULL
 *
 * Gets a time value from a local setting, falling back to the config file.
 *
 * Return value: Data value, or 0 for an error
 *
 * Since: 0.1.0
 **/
guint
zif_config_get_time (ZifConfig *config,
		     const gchar *key,
		     GError **error)
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
 * zif_config_get_enum:
 * @config: A #ZifConfig
 * @key: A key name to retrieve, e.g. "pkg_compare_mode"
 * @func: (scope call): A #ZifConfigEnumMappingFunc to convert the string to an enum
 * @error: A #GError, or %NULL
 *
 * Gets an enumerated value from a local setting, falling back to the
 * config file.
 *
 * Return value: Enumerated value, or %G_MAXUINT for an error
 *
 * Since: 0.2.3
 **/
guint
zif_config_get_enum (ZifConfig *config,
		     const gchar *key,
		     ZifConfigEnumMappingFunc func,
		     GError **error)
{
	gchar *tmp;
	guint value = G_MAXUINT;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), G_MAXUINT);
	g_return_val_if_fail (key != NULL, G_MAXUINT);
	g_return_val_if_fail (func != NULL, G_MAXUINT);
	g_return_val_if_fail (error == NULL || *error == NULL, G_MAXUINT);

	/* get string value */
	tmp = zif_config_get_string (config, key, error);
	if (tmp == NULL)
		goto out;

	/* convert */
	value = func (tmp);
out:
	g_free (tmp);
	return value;
}

/**
 * zif_config_expand_substitutions:
 * @config: A #ZifConfig
 * @text: The string to scan, e.g. "http://fedora/$releasever/$basearch/moo.rpm"
 * @error: A #GError, or %NULL
 *
 * Replaces substitutions in text with the actual values of the running system.
 *
 * Return value: A new allocated string or %NULL for error
 *
 * Since: 0.1.0
 **/
gchar *
zif_config_expand_substitutions (ZifConfig *config,
				 const gchar *text,
				 GError **error)
{
	gchar *basearch = NULL;
	gchar *releasever = NULL;
	GString *string = NULL;
	gchar *retval = NULL;

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
	string = g_string_new (text);
	zif_string_replace (string, "$releasever", releasever);
	zif_string_replace (string, "$basearch", basearch);
	zif_string_replace (string, "$srcdir", TOP_SRCDIR);

	/* success */
	retval = g_string_free (string, FALSE);
out:
	g_free (basearch);
	g_free (releasever);
	return retval;
}

/**
 * zif_config_get_basearch_array:
 * @config: A #ZifConfig
 *
 * Gets the list of architectures that packages are native on for this machine.
 *
 * Return value: (transfer none): An array of strings, e.g. [ "i386", "i486", "noarch" ]
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
 * zif_config_set_filename:
 * @config: A #ZifConfig
 * @filename: A system wide config file, e.g. "/etc/zif/zif.conf", or %NULL to use the default.
 * @error: A #GError, or %NULL
 *
 * Sets the filename to use as the system wide config file.
 *
 * Using @filename set to %NULL to use the default value
 * has been supported since 0.1.3. Earlier versions will assert.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_config_set_filename (ZifConfig *config,
			 const gchar *filename,
			 GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *basearch = NULL;
	const gchar *text;
	GPtrArray *array;
	gchar *filename_override = NULL;
	gchar *filename_override_sub = NULL;
	guint i;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already loaded */
	if (config->priv->loaded) {
		g_set_error_literal (error,
				     ZIF_CONFIG_ERROR,
				     ZIF_CONFIG_ERROR_FAILED,
				     "config already loaded");
		goto out;
	}

	/* do we use te default? */
	if (filename == NULL) {
		config->priv->filename = g_build_filename (SYSCONFDIR,
							   "zif",
							   "zif.conf",
							   NULL);
	} else {
		config->priv->filename = g_strdup (filename);
	}

	/* check file exists */
	g_debug ("using config %s", config->priv->filename);
	ret = g_file_test (config->priv->filename, G_FILE_TEST_IS_REGULAR);
	if (!ret) {
		g_set_error (error,
			     ZIF_CONFIG_ERROR,
			     ZIF_CONFIG_ERROR_FAILED,
			     "config file %s does not exist",
			     config->priv->filename);
		goto out;
	}

	/* setup watch */
	ret = zif_monitor_add_watch (config->priv->monitor,
				     config->priv->filename,
				     &error_local);
	if (!ret) {
		g_set_error (error, ZIF_CONFIG_ERROR,
			     ZIF_CONFIG_ERROR_FAILED,
			     "failed to setup watch: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* calculate the valid basearchs */
	basearch = zif_config_get_string (config, "basearch", &error_local);
	if (basearch == NULL) {
		g_set_error (error,
			     ZIF_CONFIG_ERROR,
			     ZIF_CONFIG_ERROR_FAILED,
			     "failed to get basearch: %s",
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* get override file */
	filename_override = g_key_file_get_string (config->priv->file_default,
						   "main",
						   "override_config",
						   NULL);

	/* expand out, in case the override_config contains $srcdir */
	if (filename_override != NULL) {
		filename_override_sub = zif_config_expand_substitutions (config,
									 filename_override,
									 NULL);
	}

	/* load override file */
	if (filename_override_sub == NULL) {
		g_debug ("no override file specified");
	} else if (g_file_test (filename_override_sub,
				G_FILE_TEST_EXISTS)) {
		g_debug ("using override file %s",
			 filename_override_sub);
		ret = g_key_file_load_from_file (config->priv->file_override,
						 filename_override_sub,
						 G_KEY_FILE_NONE,
						 &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_CONFIG_ERROR,
				     ZIF_CONFIG_ERROR_FAILED,
				     "failed to load config file: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	} else {
		g_debug ("override file %s does not exist",
			 filename_override_sub);
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
	for (i = 0; i < array->len; i++) {
		text = g_ptr_array_index (array, i);
		config->priv->basearch_list[i] = g_strdup (text);
	}
	g_ptr_array_unref (array);
out:
	g_free (filename_override);
	g_free (filename_override_sub);
	g_free (basearch);
	return ret;
}

/**
 * zif_config_reset_default:
 * @config: A #ZifConfig
 * @error: A #GError, or %NULL
 *
 * Removes any local settings previously set.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_config_reset_default (ZifConfig *config, GError **error)
{
	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_hash_table_remove_all (config->priv->hash_override);
	return TRUE;
}

/**
 * zif_config_set_local:
 * @config: A #ZifConfig
 * @key: Key name to save, e.g. "keepcache"
 * @value: Key data to save, e.g. "always"
 * @error: A #GError, or %NULL
 *
 * Sets a local value which is used in preference to the config value.
 * This is deprecated. Use zif_config_set_string(), zif_config_set_uint()
 * and zif_config_set_boolean() instead.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_config_set_local (ZifConfig *config,
		      const gchar *key,
		      const gchar *value,
		      GError **error)
{
	g_warning ("This is deprecated. Use zif_config_set_[string|uint|bool] instead");
	return zif_config_set_string (config, key, value, error);
}

/**
 * zif_config_set_string:
 * @config: A #ZifConfig
 * @key: Key name to save, e.g. "keepcache"
 * @value: Key data to save, e.g. "always"
 * @error: A #GError, or %NULL
 *
 * Sets a local value which is used in preference to the config value.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.2
 **/
gboolean
zif_config_set_string (ZifConfig *config,
		       const gchar *key,
		       const gchar *value,
		       GError **error)
{
	const gchar *value_tmp;
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already exists? */
	value_tmp = g_hash_table_lookup (config->priv->hash_override, key);
	if (value_tmp != NULL) {
		/* already set to the same value */
		if (g_strcmp0 (value_tmp, value) == 0)
			goto out;
		g_set_error (error,
			     ZIF_CONFIG_ERROR,
			     ZIF_CONFIG_ERROR_FAILED,
			     "already set key %s to %s, cannot overwrite with %s",
			     key, value_tmp, value);
		ret = FALSE;
		goto out;
	}

	/* insert into table */
	g_hash_table_insert (config->priv->hash_override,
			     g_strdup (key),
			     g_strdup (value));
out:
	return ret;
}

/**
 * zif_config_set_default:
 **/
static void
zif_config_set_default (ZifConfig *config,
			const gchar *key,
			const gchar *value)
{
	/* just insert into table */
	g_hash_table_insert (config->priv->hash_default,
			     g_strdup (key),
			     g_strdup (value));
}

/**
 * zif_config_set_boolean:
 * @config: A #ZifConfig
 * @key: Key name to save, e.g. "keepcache"
 * @value: Key data, e.g. %TRUE
 * @error: A #GError, or %NULL
 *
 * Sets a local value which is used in preference to the config value.
 * %TRUE is saved as "true" and %FALSE is saved as "false"
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.2
 **/
gboolean
zif_config_set_boolean (ZifConfig *config,
			const gchar *key,
			gboolean value,
			GError **error)
{
	return zif_config_set_string (config,
				      key,
				      value ? "true" : "false",
				      error);
}

/**
 * zif_config_set_uint:
 * @config: A #ZifConfig
 * @key: Key name to save, e.g. "keepcache"
 * @value: Key data, e.g. %TRUE
 * @error: A #GError, or %NULL
 *
 * Sets a local value which is used in preference to the config value.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.2
 **/
gboolean
zif_config_set_uint (ZifConfig *config,
		     const gchar *key,
		     guint value,
		     GError **error)
{
	gboolean ret;
	gchar *temp;
	temp = g_strdup_printf ("%i", value);
	ret = zif_config_set_string (config, key, temp, error);
	g_free (temp);
	return ret;
}

/**
 * zif_config_file_monitor_cb:
 **/
static void
zif_config_file_monitor_cb (ZifMonitor *monitor, ZifConfig *config)
{
	g_debug ("config file changed");
	zif_config_unload (config, NULL);
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

	g_key_file_free (config->priv->file_override);
	g_key_file_free (config->priv->file_default);
	g_hash_table_unref (config->priv->hash_override);
	g_hash_table_unref (config->priv->hash_default);
	g_signal_handler_disconnect (config->priv->monitor,
				     config->priv->monitor_changed_id);
	g_object_unref (config->priv->monitor);
	g_strfreev (config->priv->basearch_list);
	g_free (config->priv->filename);

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
	const gchar *value;

	/* make sure initialized */
	zif_init ();

	config->priv = ZIF_CONFIG_GET_PRIVATE (config);
	config->priv->file_override = g_key_file_new ();
	config->priv->file_default = g_key_file_new ();
	config->priv->loaded = FALSE;
	config->priv->hash_override = g_hash_table_new_full (g_str_hash,
							     g_str_equal,
							     g_free,
							     g_free);
	config->priv->hash_default = g_hash_table_new_full (g_str_hash,
							    g_str_equal,
							    g_free,
							    g_free);
	config->priv->basearch_list = NULL;
	config->priv->monitor = zif_monitor_new ();
	config->priv->monitor_changed_id =
		g_signal_connect (config->priv->monitor, "changed",
				  G_CALLBACK (zif_config_file_monitor_cb),
				  config);

	/* get info from RPM */
	rpmGetOsInfo (&value, NULL);
	zif_config_set_default (config, "osinfo", value);
	rpmGetArchInfo (&value, NULL);
	zif_config_set_default (config, "archinfo", value);
	rpmGetArchInfo (&value, NULL);
	if (g_strcmp0 (value, "i486") == 0 ||
	    g_strcmp0 (value, "i586") == 0 ||
	    g_strcmp0 (value, "i686") == 0)
		value = "i386";
	if (g_strcmp0 (value, "armv7l") == 0 ||
	    g_strcmp0 (value, "armv6l") == 0 ||
	    g_strcmp0 (value, "armv5tejl") == 0 ||
	    g_strcmp0 (value, "armv5tel") == 0)
		value = "arm";
	if (g_strcmp0 (value, "armv7hnl") == 0 ||
	    g_strcmp0 (value, "armv7hl") == 0)
		value = "armhfp";
	zif_config_set_default (config, "basearch", value);
}

/**
 * zif_config_new:
 *
 * Return value: A new #ZifConfig instance.
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
		g_object_add_weak_pointer (zif_config_object,
					   &zif_config_object);
	}
	return ZIF_CONFIG (zif_config_object);
}

