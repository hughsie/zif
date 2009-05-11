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

#include <string.h>

#include <glib.h>
#include <packagekit-glib/packagekit.h>
#include <rpm/rpmlib.h>

#include "dum-config.h"
#include "dum-utils.h"
#include "dum-monitor.h"

#include "egg-debug.h"
#include "egg-string.h"

#define DUM_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_CONFIG, DumConfigPrivate))

struct DumConfigPrivate
{
	GKeyFile		*keyfile;
	gboolean		 loaded;
	DumMonitor		*monitor;
};

G_DEFINE_TYPE (DumConfig, dum_config, G_TYPE_OBJECT)
static gpointer dum_config_object = NULL;

/**
 * dum_config_get_string:
 **/
gchar *
dum_config_get_string (DumConfig *config, const gchar *key, GError **error)
{
	gchar *value;
	const gchar *info;
	GError *error_local = NULL;

	g_return_val_if_fail (DUM_IS_CONFIG (config), NULL);
	g_return_val_if_fail (config->priv->loaded, FALSE);
	g_return_val_if_fail (key != NULL, NULL);

	value = g_key_file_get_string (config->priv->keyfile, "main", key, &error_local);
	if (value == NULL) {

		/* special keys, FIXME: add to yum */
		if (strcmp (key, "reposdir") == 0) {
			value = g_strdup ("/etc/yum.repos.d");
			goto out;
		}

		/* special rpmkeys */
		if (strcmp (key, "osinfo") == 0) {
			rpmGetOsInfo (&info, NULL);
			value = g_strdup (info);
			goto out;
		}
		if (strcmp (key, "archinfo") == 0) {
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
 * dum_config_get_boolean:
 **/
gboolean
dum_config_get_boolean (DumConfig *config, const gchar *key, GError **error)
{
	gchar *value;
	gboolean ret = FALSE;

	g_return_val_if_fail (DUM_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (config->priv->loaded, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	value = dum_config_get_string (config, key, error);
	if (value == NULL)
		goto out;

	/* convert to bool */
	ret = dum_boolean_from_text (value);

	g_free (value);
out:
	return ret;
}

/**
 * dum_config_set_filename:
 **/
gboolean
dum_config_set_filename (DumConfig *config, const gchar *filename, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (DUM_IS_CONFIG (config), FALSE);
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
	ret = dum_monitor_add_watch (config->priv->monitor, filename, &error_local);
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
 * dum_config_file_monitor_cb:
 **/
static void
dum_config_file_monitor_cb (DumMonitor *monitor, DumConfig *config)
{
	egg_warning ("config file changed");
	config->priv->loaded = FALSE;
}

/**
 * dum_config_finalize:
 **/
static void
dum_config_finalize (GObject *object)
{
	DumConfig *config;
	g_return_if_fail (DUM_IS_CONFIG (object));
	config = DUM_CONFIG (object);

	g_key_file_free (config->priv->keyfile);
	g_object_unref (config->priv->monitor);

	G_OBJECT_CLASS (dum_config_parent_class)->finalize (object);
}

/**
 * dum_config_class_init:
 **/
static void
dum_config_class_init (DumConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_config_finalize;
	g_type_class_add_private (klass, sizeof (DumConfigPrivate));
}

/**
 * dum_config_init:
 **/
static void
dum_config_init (DumConfig *config)
{
	config->priv = DUM_CONFIG_GET_PRIVATE (config);
	config->priv->keyfile = g_key_file_new ();
	config->priv->loaded = FALSE;
	config->priv->monitor = dum_monitor_new ();
	g_signal_connect (config->priv->monitor, "changed", G_CALLBACK (dum_config_file_monitor_cb), config);
}

/**
 * dum_config_new:
 * Return value: A new config class instance.
 **/
DumConfig *
dum_config_new (void)
{
	if (dum_config_object != NULL) {
		g_object_ref (dum_config_object);
	} else {
		dum_config_object = g_object_new (DUM_TYPE_CONFIG, NULL);
		g_object_add_weak_pointer (dum_config_object, &dum_config_object);
	}
	return DUM_CONFIG (dum_config_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_config_test (EggTest *test)
{
	DumConfig *config;
	gboolean ret;
	GError *error = NULL;
	gchar *value;

	if (!egg_test_start (test, "DumConfig"))
		return;

	/************************************************************/
	egg_test_title (test, "get config");
	config = dum_config_new ();
	egg_test_assert (test, config != NULL);

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = dum_config_set_filename (config, "../test/etc/yum.conf", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set filename '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "get cachedir");
	value = dum_config_get_string (config, "cachedir", NULL);
	if (egg_strequal (value, "../test/cache"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value '%s'", value);
	g_free (value);

	/************************************************************/
	egg_test_title (test, "get cachexxxdir");
	value = dum_config_get_string (config, "cachexxxdir", NULL);
	if (value == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value '%s'", value);
	g_free (value);

	/************************************************************/
	egg_test_title (test, "get exactarch");
	ret = dum_config_get_boolean (config, "exactarch", NULL);
	egg_test_assert (test, ret);

	g_object_unref (config);

	egg_test_end (test);
}
#endif

