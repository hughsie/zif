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
 * SECTION:zif-release
 * @short_description: A #ZifRelease object allows the user to check licenses
 *
 * #ZifRelease allows the user to see if a specific license string is free
 * according to the FSF.
 * Before checking any strings, the backing release file has to be set with
 * zif_release_set_filename() and any checks prior to that will fail.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-release.h"
#include "zif-monitor.h"
#include "zif-download.h"

#define ZIF_RELEASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_RELEASE, ZifReleasePrivate))

struct _ZifReleasePrivate
{
	gboolean		 loaded;
	ZifMonitor		*monitor;
	ZifDownload		*download;
	GPtrArray		*array;
	gchar			*filename;
	gchar			*uri;
	guint			 monitor_changed_id;
};

G_DEFINE_TYPE (ZifRelease, zif_release, G_TYPE_OBJECT)
static gpointer zif_release_object = NULL;

/**
 * zif_release_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.1.3
 **/
GQuark
zif_release_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_release_error");
	return quark;
}

/**
 * zif_release_load:
 **/
static gboolean
zif_release_load (ZifRelease *release, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	GKeyFile *key_file = NULL;
	gchar **groups = NULL;
	ZifUpgrade *upgrade;
	guint i;
	const gchar *temp;

	/* nothing set */
	if (release->priv->filename == NULL) {
		g_set_error_literal (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
				     "no release filename has been set; use zif_release_set_filename()");
		goto out;
	}

	/* download if it does not already exist */
	ret = g_file_test (release->priv->filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		ret = zif_download_file (release->priv->download,
					 release->priv->uri,
					 release->priv->filename,
					 state, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
				     "failed to download release info: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* setup watch */
	ret = zif_monitor_add_watch (release->priv->monitor, release->priv->filename, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
			     "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* open the releases file */
	key_file = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file, release->priv->filename, 0, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
			     "failed to open release info %s: %s",
			     release->priv->filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get all the sections in releases.txt */
	groups = g_key_file_get_groups (key_file, NULL);
	if (groups == NULL) {
		g_set_error_literal (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
				     "releases.txt has no groups");
		goto out;
	}

	/* find our release version in each one */
	for (i=0; groups[i] != NULL; i++) {
		upgrade = zif_upgrade_new ();
		g_debug ("adding %s", groups[i]);
		zif_upgrade_set_id (upgrade, groups[i]);
		temp = g_key_file_get_string (key_file, groups[i], "stable", NULL);
		if (g_strcmp0 (temp, "True") == 0)
			zif_upgrade_set_stable (upgrade, TRUE);
		temp = g_key_file_get_string (key_file, groups[i], "preupgrade-ok", NULL);
		if (g_strcmp0 (temp, "True") == 0)
			zif_upgrade_set_enabled (upgrade, TRUE);
		zif_upgrade_set_version (upgrade, g_key_file_get_integer (key_file, groups[i], "version", NULL));
		temp = g_key_file_get_string (key_file, groups[i], "baseurl", NULL);
		if (temp != NULL)
			zif_upgrade_set_baseurl (upgrade, temp);
		temp = g_key_file_get_string (key_file, groups[i], "mirrorlist", NULL);
		if (temp != NULL)
			zif_upgrade_set_mirrorlist (upgrade, temp);
		temp = g_key_file_get_string (key_file, groups[i], "installmirrorlist", NULL);
		if (temp != NULL)
			zif_upgrade_set_install_mirrorlist (upgrade, temp);
		g_ptr_array_add (release->priv->array, upgrade);
	}

	/* done */
	release->priv->loaded = TRUE;
out:
	g_strfreev (groups);
	if (key_file != NULL)
		g_key_file_free (key_file);
	return ret;
}

/**
 * zif_release_get_upgrades:
 * @release: the #ZifRelease object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets all the upgrades, older and newer.
 *
 * Return value: A #GPtrArray of #ZifUpgrade's, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_release_get_upgrades (ZifRelease *release, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_RELEASE (release), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	if (!release->priv->loaded) {
		ret = zif_release_load (release, state, error);
		if (!ret)
			goto out;
	}

	/* success */
	array = g_ptr_array_ref (release->priv->array);
out:
	return array;
}

/**
 * zif_release_get_upgrades_new:
 * @release: the #ZifRelease object
 * @version: a distribution version, e.g. 15
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets all the upgrades older than the one currently installed.
 *
 * Return value: A #GPtrArray of #ZifUpgrade's, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_release_get_upgrades_new (ZifRelease *release, guint version, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	gboolean ret;
	guint i;
	ZifUpgrade *upgrade;

	g_return_val_if_fail (ZIF_IS_RELEASE (release), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	if (!release->priv->loaded) {
		ret = zif_release_load (release, state, error);
		if (!ret)
			goto out;
	}

	/* success */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<release->priv->array->len; i++) {
		upgrade = g_ptr_array_index (release->priv->array, i);
		//FIXME: get from config file
		if (zif_upgrade_get_version (upgrade) > 14)
			g_ptr_array_add (array, g_object_ref (upgrade));
	}
out:
	return array;
}

/**
 * zif_release_get_upgrade_for_version:
 * @release: the #ZifRelease object
 * @version: a distribution version, e.g. 15
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets a specific upgrade object for the given version.
 *
 * Return value: A #ZifUpgrade, free with g_object_unref()
 *
 * Since: 0.1.3
 **/
ZifUpgrade *
zif_release_get_upgrade_for_version (ZifRelease *release, guint version, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	guint i;
	ZifUpgrade *upgrade = NULL;
	ZifUpgrade *upgrade_tmp;

	g_return_val_if_fail (ZIF_IS_RELEASE (release), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find upgrade */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<release->priv->array->len; i++) {
		upgrade_tmp = g_ptr_array_index (release->priv->array, i);
		if (zif_upgrade_get_version (upgrade_tmp) == version) {
			upgrade = g_object_ref (upgrade_tmp);
			goto out;
		}
	}

	/* nothing found */
	g_set_error (error,
		     ZIF_RELEASE_ERROR,
		     ZIF_RELEASE_ERROR_FAILED,
		     "could not find upgrade version %i", version);
out:
	return upgrade;
}

/**
 * zif_release_upgrade_version:
 * @release: the #ZifRelease object
 * @version: a distribution version, e.g. 15
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Upgrade the distribution to a given version.
 *
 * Return value: A #ZifUpgrade, free with g_object_unref()
 *
 * Since: 0.1.3
 **/
gboolean
zif_release_upgrade_version (ZifRelease *release, guint version, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	ZifUpgrade *upgrade;

	g_return_val_if_fail (ZIF_IS_RELEASE (release), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get the correct object */
	upgrade = zif_release_get_upgrade_for_version	(release, version, state, error);
	if (upgrade == NULL)
		goto out;

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_release_set_filename:
 * @release: the #ZifRelease object
 * @filename: the system wide release file, e.g. "/var/cache/PackageKit/releases.txt"
 *
 * Sets the filename to use as the local file cache.
 *
 * Since: 0.1.3
 **/
void
zif_release_set_filename (ZifRelease *release, const gchar *filename)
{
	g_return_if_fail (ZIF_IS_RELEASE (release));
	g_return_if_fail (filename != NULL);
	g_return_if_fail (!release->priv->loaded);

	g_free (release->priv->filename);
	release->priv->filename = g_strdup (filename);
}

/**
 * zif_release_set_uri:
 * @release: the #ZifRelease object
 * @uri: the remote URI, e.g. "http://people.freedesktop.org/~hughsient/fedora/preupgrade/releases.txt"
 *
 * Sets the URI to use as the release information file.
 *
 * Since: 0.1.3
 **/
void
zif_release_set_uri (ZifRelease *release, const gchar *uri)
{
	g_return_if_fail (ZIF_IS_RELEASE (release));
	g_return_if_fail (uri != NULL);
	g_return_if_fail (!release->priv->loaded);

	g_free (release->priv->uri);
	release->priv->uri = g_strdup (uri);
}

/**
 * zif_release_file_monitor_cb:
 **/
static void
zif_release_file_monitor_cb (ZifMonitor *monitor, ZifRelease *release)
{
	g_warning ("release file changed");
	g_ptr_array_set_size (release->priv->array, 0);
	release->priv->loaded = FALSE;
}

/**
 * zif_release_finalize:
 **/
static void
zif_release_finalize (GObject *object)
{
	ZifRelease *release;
	g_return_if_fail (ZIF_IS_RELEASE (object));
	release = ZIF_RELEASE (object);

	g_ptr_array_unref (release->priv->array);
	g_signal_handler_disconnect (release->priv->monitor, release->priv->monitor_changed_id);
	g_object_unref (release->priv->monitor);
	g_object_unref (release->priv->download);
	g_free (release->priv->filename);
	g_free (release->priv->uri);

	G_OBJECT_CLASS (zif_release_parent_class)->finalize (object);
}

/**
 * zif_release_class_init:
 **/
static void
zif_release_class_init (ZifReleaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_release_finalize;
	g_type_class_add_private (klass, sizeof (ZifReleasePrivate));
}

/**
 * zif_release_init:
 **/
static void
zif_release_init (ZifRelease *release)
{
	release->priv = ZIF_RELEASE_GET_PRIVATE (release);
	release->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	release->priv->download = zif_download_new ();
	release->priv->monitor = zif_monitor_new ();
	release->priv->monitor_changed_id =
		g_signal_connect (release->priv->monitor, "changed",
				  G_CALLBACK (zif_release_file_monitor_cb), release);
}

/**
 * zif_release_new:
 *
 * Return value: A new #ZifRelease class instance.
 *
 * Since: 0.1.3
 **/
ZifRelease *
zif_release_new (void)
{
	if (zif_release_object != NULL) {
		g_object_ref (zif_release_object);
	} else {
		zif_release_object = g_object_new (ZIF_TYPE_RELEASE, NULL);
		g_object_add_weak_pointer (zif_release_object, &zif_release_object);
	}
	return ZIF_RELEASE (zif_release_object);
}

