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

/**
 * SECTION:zif-config
 * @short_description: A #ZifConfig object manages system wide config options
 *
 * #ZifConfig allows settings to be read from a central config file.
 */

#include <string.h>

#include <glib.h>
#include <packagekit-glib/packagekit.h>
#include <rpm/rpmlib.h>

#include "zif-config.h"
#include "zif-utils.h"
#include "zif-monitor.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_CONFIG, ZifConfigPrivate))

struct _ZifConfigPrivate
{
	GKeyFile		*keyfile;
	gboolean		 loaded;
	ZifMonitor		*monitor;
};

G_DEFINE_TYPE (ZifConfig, zif_config, G_TYPE_OBJECT)
static gpointer zif_config_object = NULL;

/**
 * zif_config_get_string:
 * @config: the #ZifConfig object
 * @key: the key name to retrieve, e.g. "cachedir"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets a string value from the config file.
 *
 * Return value: the allocated value, or %NULL
 **/
gchar *
zif_config_get_string (ZifConfig *config, const gchar *key, GError **error)
{
	gchar *value = NULL;
	const gchar *info;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* not loaded yet */
	if (!config->priv->loaded) {
		if (error != NULL)
			*error = g_error_new (1, 0, "config not loaded");
		goto out;
	}

	/* get value */
	value = g_key_file_get_string (config->priv->keyfile, "main", key, &error_local);
	if (value == NULL) {

		/* special keys, FIXME: add to yum */
		if (g_strcmp0 (key, "reposdir") == 0) {
			value = g_strdup ("/etc/yum.repos.d");
			goto out;
		}
		if (g_strcmp0 (key, "pidfile") == 0) {
			value = g_strdup ("/var/run/yum.pid");
			goto out;
		}

		/* special rpmkeys */
		if (g_strcmp0 (key, "osinfo") == 0) {
			rpmGetOsInfo (&info, NULL);
			value = g_strdup (info);
			goto out;
		}
		if (g_strcmp0 (key, "archinfo") == 0) {
			rpmGetArchInfo (&info, NULL);
			value = g_strdup (info);
			goto out;
		}

		if (error != NULL)
			*error = g_error_new (1, 0, "failed to read %s: %s", key, error_local->message);
		g_error_free (error_local);
	}
out:
	return value;
}

/**
 * zif_config_get_boolean:
 * @config: the #ZifConfig object
 * @key: the key name to retrieve, e.g. "keepcache"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets a boolean value from the config file.
 *
 * Return value: %TRUE or %FALSE
 **/
gboolean
zif_config_get_boolean (ZifConfig *config, const gchar *key, GError **error)
{
	gchar *value;
	gboolean ret = FALSE;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* get string value */
	value = zif_config_get_string (config, key, error);
	if (value == NULL)
		goto out;

	/* convert to bool */
	ret = zif_boolean_from_text (value);

	g_free (value);
out:
	return ret;
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
 **/
gboolean
zif_config_set_filename (ZifConfig *config, const gchar *filename, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (!config->priv->loaded, FALSE);

	/* check file exists */
	ret = g_file_test (filename, G_FILE_TEST_IS_REGULAR);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "config file %s does not exist", filename);
		goto out;
	}

	/* setup watch */
	ret = zif_monitor_add_watch (config->priv->monitor, filename, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* load file */
	ret = g_key_file_load_from_file (config->priv->keyfile, filename, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load config file: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* done */
	config->priv->loaded = TRUE;
out:
	return ret;
}

/**
 * zif_config_file_monitor_cb:
 **/
static void
zif_config_file_monitor_cb (ZifMonitor *monitor, ZifConfig *config)
{
	egg_warning ("config file changed");
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
	g_object_unref (config->priv->monitor);

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
	config->priv->monitor = zif_monitor_new ();
	g_signal_connect (config->priv->monitor, "changed", G_CALLBACK (zif_config_file_monitor_cb), config);
}

/**
 * zif_config_new:
 *
 * Return value: A new #ZifConfig class instance.
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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_config_test (EggTest *test)
{
	ZifConfig *config;
	gboolean ret;
	GError *error = NULL;
	gchar *value;

	if (!egg_test_start (test, "ZifConfig"))
		return;

	/************************************************************/
	egg_test_title (test, "get config");
	config = zif_config_new ();
	egg_test_assert (test, config != NULL);

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_config_set_filename (config, "../test/etc/yum.conf", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set filename '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "get cachedir");
	value = zif_config_get_string (config, "cachedir", NULL);
	if (egg_strequal (value, "../test/cache"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value '%s'", value);
	g_free (value);

	/************************************************************/
	egg_test_title (test, "get cachexxxdir");
	value = zif_config_get_string (config, "cachexxxdir", NULL);
	if (value == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value '%s'", value);
	g_free (value);

	/************************************************************/
	egg_test_title (test, "get exactarch");
	ret = zif_config_get_boolean (config, "exactarch", NULL);
	egg_test_assert (test, ret);

	g_object_unref (config);

	egg_test_end (test);
}
#endif

