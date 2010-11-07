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
 * @short_description: A #ZifRelease object allows the user check for
 * distribution upgrades.
 *
 * #ZifRelease allows the user to check for distribution upgrades and
 * upgrade to the newest release.
 *
 * Before checking for upgrades, the releases release file has to be set
 * with zif_release_set_cache_dir() and any checks prior to that will fail.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "zif-config.h"
#include "zif-release.h"
#include "zif-monitor.h"
#include "zif-download.h"
#include "zif-md-mirrorlist.h"

#define ZIF_RELEASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_RELEASE, ZifReleasePrivate))

struct _ZifReleasePrivate
{
	gboolean		 loaded;
	ZifMonitor		*monitor;
	ZifDownload		*download;
	ZifConfig		*config;
	GPtrArray		*array;
	gchar			*cache_dir;
	gchar			*boot_dir;
	gchar			*uri;
	guint			 monitor_changed_id;
};

/* only used when doing an upgrade */
typedef struct {
	ZifUpgrade		*upgrade;
	ZifReleaseUpgradeKind	 upgrade_kind;
	guint			 version;
	GKeyFile		*key_file_treeinfo;
	gchar			*images_section;
} ZifReleaseUpgradeData;

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
	gchar *temp_expand;
	gchar *filename = NULL;

	/* nothing set */
	if (release->priv->cache_dir == NULL) {
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_SETUP_INVALID,
				     "no cache dir has been set; use zif_release_set_cache_dir()");
		goto out;
	}

	/* download if it does not already exist */
	filename = g_build_filename (release->priv->cache_dir, "releases.txt", NULL);
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		ret = zif_download_file (release->priv->download,
					 release->priv->uri,
					 filename,
					 state, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_DOWNLOAD_FAILED,
				     "failed to download release info: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* setup watch */
	ret = zif_monitor_add_watch (release->priv->monitor, filename, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_SETUP_INVALID,
			     "failed to setup watch: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* open the releases file */
	key_file = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file, filename, 0, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_FILE_INVALID,
			     "failed to open release info %s: %s",
			     filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get all the sections in releases.txt */
	groups = g_key_file_get_groups (key_file, NULL);
	if (groups == NULL) {
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_FILE_INVALID,
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
		if (temp != NULL) {
			temp_expand = zif_config_expand_substitutions (release->priv->config, temp, NULL);
			zif_upgrade_set_baseurl (upgrade, temp_expand);
			g_free (temp_expand);
		}
		temp = g_key_file_get_string (key_file, groups[i], "mirrorlist", NULL);
		if (temp != NULL) {
			temp_expand = zif_config_expand_substitutions (release->priv->config, temp, NULL);
			zif_upgrade_set_mirrorlist (upgrade, temp_expand);
			g_free (temp_expand);
		}
		temp = g_key_file_get_string (key_file, groups[i], "installmirrorlist", NULL);
		if (temp != NULL) {
			temp_expand = zif_config_expand_substitutions (release->priv->config, temp, NULL);
			zif_upgrade_set_install_mirrorlist (upgrade, temp_expand);
			g_free (temp_expand);
		}
		g_ptr_array_add (release->priv->array, upgrade);
	}

	/* done */
	release->priv->loaded = TRUE;
out:
	g_free (filename);
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
		if (zif_upgrade_get_version (upgrade) > version)
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
	guint i;
	gboolean ret;
	ZifUpgrade *upgrade = NULL;
	ZifUpgrade *upgrade_tmp;

	g_return_val_if_fail (ZIF_IS_RELEASE (release), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not loaded yet */
	if (!release->priv->loaded) {
		ret = zif_release_load (release, state, error);
		if (!ret)
			goto out;
	}

	/* find upgrade */
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
		     ZIF_RELEASE_ERROR_NOT_FOUND,
		     "could not find upgrade version %i", version);
out:
	return upgrade;
}

/**
 * zif_release_add_kernel:
 **/
static gboolean
zif_release_add_kernel (ZifRelease *release, ZifReleaseUpgradeData *data, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GString *cmdline = NULL;
	GString *args = NULL;
	gchar *title = NULL;
	gchar *arch = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* yaboot (ppc) doesn't support spaces in titles */
	arch = zif_config_get_string (priv->config, "basearch", NULL);
	if (g_str_has_prefix (arch, "ppc")) {
		title = g_strdup ("upgrade");
	} else {
		title = g_strdup_printf ("Upgrade to Fedora %i",
					 zif_upgrade_get_version (data->upgrade));
	}

	/* kernel arguments */
	args = g_string_new ("preupgrade ");
	if (data->upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_DEFAULT ||
	    data->upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_COMPLETE) {
		g_string_append_printf (args,
					"stage2=%s/stage2.img ", priv->boot_dir);
	}
	g_string_append_printf (args,
			        "ksdevice=link ip=dhcp ipv6=dhcp");

	/* do for i386 and ppc */
	cmdline = g_string_new ("/sbin/grubby ");
	g_string_append_printf (cmdline,
			        "--add-kernel=%s/vmlinuz ", priv->boot_dir);
	g_string_append_printf (cmdline,
			        "--initrd=%s/initrd.img ", priv->boot_dir);
	g_string_append_printf (cmdline,
			        "--title=\"%s\" ", title);
	g_string_append_printf (cmdline,
			        "--args=\"%s\"", args->str);

	/* we're not running as root */
	if (!g_str_has_prefix (priv->boot_dir, "/boot")) {
		g_debug ("not running grubby as not installing root, would have run '%s'", cmdline->str);
		ret = TRUE;
		goto out;
	}

	/* run the command */
	g_debug ("running command %s", cmdline->str);
	ret = g_spawn_command_line_sync (cmdline->str, NULL, NULL, NULL, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_SPAWN_FAILED,
			     "failed to add kernel: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* ppc machines need to run ybin to activate changes */
	if (g_str_has_prefix (arch, "ppc")) {
		g_debug ("running ybin command");
		ret = g_spawn_command_line_sync ("/sbin/ybin > /dev/null", NULL, NULL, NULL, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_SPAWN_FAILED,
				     "failed to run: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	g_free (arch);
	g_free (title);
	if (args != NULL)
		g_string_free (args, TRUE);
	if (cmdline != NULL)
		g_string_free (cmdline, TRUE);
	return ret;
}

/**
 * zif_release_make_kernel_default_once:
 **/
static gboolean
zif_release_make_kernel_default_once (ZifRelease *release, GError **error)
{
	gboolean ret = TRUE;
	const gchar *cmdline = "/bin/echo 'savedefault --default=0 --once' | /sbin/grub >/dev/null";
	GError *error_local = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* we're not running as root */
	if (!g_str_has_prefix (priv->boot_dir, "/boot")) {
		g_debug ("not running grub as not installing root, would have run '%s'", cmdline);
		goto out;
	}

	/* run the command */
	g_debug ("running command %s", cmdline);
	ret = g_spawn_command_line_sync (cmdline, NULL, NULL, NULL, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_SPAWN_FAILED,
			     "failed to make kernel default: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	return ret;
}

/**
 * zif_release_check_filesystem_size:
 **/
static gboolean
zif_release_check_filesystem_size (const gchar *location, guint64 required_size, GError **error)
{
	GFile *file;
	GFileInfo *info;
	guint64 size;
	gboolean ret = FALSE;

	/* open file */
	file = g_file_new_for_path (location);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_FILESYSTEM_FREE,
				  0, NULL, error);
	if (info == NULL)
		goto out;

	/* check has attribute */
	ret = !g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
	if (ret)
		goto out;

	/* get size on the file-system */
	size = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
	if (size < required_size) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_LOW_DISKSPACE,
			     "%s filesystem too small, requires %" G_GUINT64_FORMAT
			     " got %" G_GUINT64_FORMAT,
			     location, required_size, size);
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	if (info != NULL)
		g_object_unref (info);
	g_object_unref (file);
	return ret;
}

/**
 * pk_release_checksum_matches_file:
 **/
static gboolean
pk_release_checksum_matches_file (const gchar *filename, const gchar *sha256, ZifState *state, GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	gsize len;
	const gchar *got;
	GChecksum *checksum = NULL;

	/* set state */
	zif_state_action_start (state, ZIF_STATE_ACTION_CHECKING, filename);

	/* load file */
	ret = g_file_get_contents (filename, &data, &len, error);
	if (!ret)
		goto out;
	checksum = g_checksum_new (G_CHECKSUM_SHA256);
	g_checksum_update (checksum, (guchar*)data, len);
	got = g_checksum_get_string (checksum);
	ret = (g_strcmp0 (sha256, got) == 0);
	if (!ret) {
		g_set_error (error, 1, 0, "checksum failed to match");
		goto out;
	}
out:
	zif_state_action_stop (state);
	g_free (data);
	if (checksum != NULL)
		g_checksum_free (checksum);
	return ret;
}

/**
 * zif_release_get_treeinfo:
 **/
static gboolean
zif_release_get_treeinfo (ZifRelease *release, ZifReleaseUpgradeData *data, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *treeinfo_uri = NULL;
	gchar *basearch = NULL;
	gchar *treeinfo_filename = NULL;
	gint version_tmp;
	ZifState *state_local;
	ZifReleasePrivate *priv = release->priv;

	/* 1. get treeinfo
	 * 2. parse it
	 */
	zif_state_set_number_steps (state, 2);

	/* get .treeinfo from a mirror in the installmirrorlist */
	treeinfo_filename = g_build_filename (priv->cache_dir, ".treeinfo", NULL);
	ret = g_file_test (treeinfo_filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		state_local = zif_state_get_child (state);
		ret = zif_download_location (priv->download, ".treeinfo", treeinfo_filename, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_DOWNLOAD_FAILED,
				     "failed to download treeinfo: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* parse the treeinfo file */
	data->key_file_treeinfo = g_key_file_new ();
	ret = g_key_file_load_from_file (data->key_file_treeinfo, treeinfo_filename, 0, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_FILE_INVALID,
			     "failed to open treeinfo: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* verify the version is sane */
	version_tmp = g_key_file_get_integer (data->key_file_treeinfo, "general", "version", NULL);
	if (version_tmp != (gint) data->version) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_FILE_INVALID,
				     "treeinfo release differs from wanted release");
		goto out;
	}

	/* get the correct section */
	basearch = zif_config_get_string (priv->config, "basearch", NULL);
	if (basearch == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_FILE_INVALID,
				     "failed to get basearch");
		goto out;
	}
	data->images_section = g_strdup_printf ("images-%s", basearch);

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_free (basearch);
	g_free (treeinfo_filename);
	g_free (treeinfo_uri);
	return ret;
}

/**
 * zif_release_get_kernel:
 **/
static gboolean
zif_release_get_kernel (ZifRelease *release, ZifReleaseUpgradeData *data, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *kernel = NULL;
	gchar *checksum = NULL;
	gchar *filename = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* get data */
	kernel = g_key_file_get_string (data->key_file_treeinfo, data->images_section, "kernel", NULL);
	if (kernel == NULL) {
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_FILE_INVALID,
				     "failed to get kernel section");
		goto out;
	}
	checksum = g_key_file_get_string (data->key_file_treeinfo, "checksums", kernel, NULL);

	/* check the checksum matches */
	filename = g_build_filename (priv->boot_dir, "vmlinuz", NULL);
	ret = pk_release_checksum_matches_file (filename, checksum+7, state, &error_local);
	if (!ret) {
		g_debug ("failed kernel checksum: %s", error_local->message);
		/* not fatal */
		g_clear_error (&error_local);
		g_unlink (filename);
	} else {
		g_debug ("%s already exists and is correct", filename);
	}

	/* download kernel */
	if (!ret) {
		ret = zif_download_location_full (priv->download, kernel, filename,
						  0, "application/octet-stream",
						  G_CHECKSUM_SHA256, checksum+7,
						  state, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_DOWNLOAD_FAILED,
				     "failed to download kernel: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	g_free (kernel);
	g_free (checksum);
	g_free (filename);
	return ret;
}

/**
 * zif_release_get_initrd:
 **/
static gboolean
zif_release_get_initrd (ZifRelease *release, ZifReleaseUpgradeData *data, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *initrd = NULL;
	gchar *checksum = NULL;
	gchar *filename = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* get data */
	initrd = g_key_file_get_string (data->key_file_treeinfo, data->images_section, "initrd", NULL);
	if (initrd == NULL) {
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_FILE_INVALID,
				     "failed to get initrd section");
		goto out;
	}
	checksum = g_key_file_get_string (data->key_file_treeinfo, "checksums", initrd, NULL);

	/* check the checksum matches */
	filename = g_build_filename (priv->boot_dir, "initrd.img", NULL);
	ret = pk_release_checksum_matches_file (filename, checksum+7, state, &error_local);
	if (!ret) {
		g_debug ("failed initrd checksum: %s", error_local->message);
		/* not fatal */
		g_clear_error (&error_local);
		g_unlink (filename);
	} else {
		g_debug ("%s already exists and is correct", filename);
	}

	/* download initrd */
	if (!ret) {
		ret = zif_download_location_full (priv->download, initrd,
						  filename,
						  0, "application/x-gzip",
						  G_CHECKSUM_SHA256, checksum+7,
						  state, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_DOWNLOAD_FAILED,
				     "failed to download initrd: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	g_free (initrd);
	g_free (checksum);
	g_free (filename);
	return ret;
}

/**
 * zif_release_get_stage2:
 **/
static gboolean
zif_release_get_stage2 (ZifRelease *release, ZifReleaseUpgradeData *data, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *stage2 = NULL;
	gchar *checksum = NULL;
	gchar *filename = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* get data */
	stage2 = g_key_file_get_string (data->key_file_treeinfo, "stage2", "mainimage", NULL);
	if (stage2 == NULL) {
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_FILE_INVALID,
				     "failed to get stage2 section");
		goto out;
	}
	checksum = g_key_file_get_string (data->key_file_treeinfo, "checksums", stage2, NULL);

	/* check the checksum matches */
	filename = g_build_filename (priv->boot_dir, "install.img", NULL);
	ret = pk_release_checksum_matches_file (filename, checksum+7, state, &error_local);
	if (!ret) {
		g_debug ("failed stage2 checksum: %s", error_local->message);
		/* not fatal */
		g_clear_error (&error_local);
		g_unlink (filename);
	} else {
		g_debug ("%s already exists and is correct", filename);
	}

	/* download stage2 */
	if (!ret) {
		ret = zif_download_location_full (priv->download, stage2,
						  filename,
						  0, "application/x-extension-img",
						  G_CHECKSUM_SHA256, checksum+7,
						  state, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_DOWNLOAD_FAILED,
				     "failed to download stage2: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	g_free (filename);
	g_free (stage2);
	g_free (checksum);
	return ret;
}

/**
 * zif_release_get_keyfile_value:
 **/
static gchar *
zif_release_get_keyfile_value (const gchar *filename, const gchar *key)
{
	GFile *file;
	gchar *data = NULL;
	gchar *lang = NULL;
	gchar **lines = NULL;
	GError *error = NULL;
	gboolean ret;
	guint len;
	guint i;

	/* load systemwide default */
	file = g_file_new_for_path (filename);
	ret = g_file_load_contents (file, NULL, &data, NULL, NULL, &error);
	if (!ret) {
		g_warning ("cannot open i18n: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* look for key */
	len = strlen (key);
	lines = g_strsplit (data, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		if (g_str_has_prefix (lines[i], key)) {
			lang = g_strdup (lines[i]+len+2);
			g_strdelimit (lang, "\"", '\0');
			break;
		}
	}
out:
	if (lang == NULL) {
		g_warning ("failed to get LANG, falling back to en_US.UTF-8");
		lang = g_strdup ("en_US.UTF-8");
	}
	g_object_unref (file);
	g_strfreev (lines);
	return lang;
}

/**
 * zif_release_get_lang:
 **/
static gchar *
zif_release_get_lang (void)
{
	gchar *lang;
	lang = zif_release_get_keyfile_value ("/etc/sysconfig/i18n", "LANG");
	if (lang == NULL) {
		lang = g_strdup ("en_US.UTF-8");
		g_warning ("failed to get LANG, falling back to %s", lang);
	}
	return lang;
}

/**
 * zif_release_get_keymap:
 **/
static gchar *
zif_release_get_keymap (void)
{
	gchar *keymap;
	keymap = zif_release_get_keyfile_value ("/etc/sysconfig/keyboard", "KEYTABLE");
	if (keymap == NULL) {
		keymap = g_strdup ("us");
		g_warning ("failed to get KEYTABLE, falling back to %s", keymap);
	}
	return keymap;
}

/**
 * zif_release_get_uuid:
 **/
static gchar *
zif_release_get_uuid (const gchar *root, GError **error)
{
	gchar *uuid = NULL;
	gchar *cmdline;
	gboolean ret;

	/* get the uuid */
	cmdline = g_strdup_printf ("/sbin/blkid -s UUID -o value %s", root);
	ret = g_spawn_command_line_sync (cmdline, &uuid, NULL, NULL, error);
	if (!ret)
		goto out;
out:
	g_free (cmdline);
	return uuid;
}

/**
 * zif_release_write_kickstart:
 **/
static gboolean
zif_release_write_kickstart (ZifRelease *release, GError **error)
{
	gchar *ks_filename;
	GFile *ks_file = NULL;
	GString *string;
	GError *error_local = NULL;
	gboolean ret = FALSE;
	gchar *uuid;
	gchar *lang = NULL;
	gchar *keymap = NULL;
	ZifReleasePrivate *priv = release->priv;

	ks_filename = g_build_filename (priv->boot_dir, "ks.cfg", NULL);
	ks_file = g_file_new_for_path (ks_filename);
	string = g_string_new ("# ks.cfg generated by Zif\n");

	/* get uuid */
	uuid = zif_release_get_uuid ("/dev/root", &error_local);
	if (uuid == NULL) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_NO_UUID_FOR_ROOT,
			     "failed to get uuid: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get system defaults */
	lang = zif_release_get_lang ();
	keymap = zif_release_get_keymap ();

	/* get kickstart */
	g_string_append_printf (string, "lang %s\n", lang);
	g_string_append_printf (string, "keyboard %s\n", keymap);
	g_string_append (string, "bootloader --upgrade --location=none\n");
	g_string_append (string, "clearpart --none\n");
	g_string_append_printf (string, "upgrade --root-device=UUID=%s\n", uuid);
	g_string_append (string, "reboot\n");
	g_string_append (string, "\n");
	g_string_append (string, "%post\n");
	g_string_append_printf (string, "grubby --remove-kernel=%s/vmlinuz\n", priv->boot_dir);
	g_string_append_printf (string, "rm -rf %s /var/cache/yum/preupgrade*\n", priv->boot_dir);
	g_string_append (string, "%end\n");

	/* write file */
	ret = g_file_replace_contents (ks_file, string->str, string->len,
				       NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION,
				       NULL, NULL, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_WRITE_FAILED,
			     "failed to write kickstart: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (lang);
	g_free (keymap);
	g_free (uuid);
	g_object_unref (ks_file);
	g_string_free (string, TRUE);
	g_free (ks_filename);
	return ret;
}

/**
 * zif_release_get_package_data:
 **/
static gboolean
zif_release_get_package_data (ZifRelease *release, ZifReleaseUpgradeData *data, ZifState *state, GError **error)
{
	//FIXME
	g_set_error_literal (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_NOT_SUPPORTED,
			     "getting the package data is not supported yet");
	return FALSE;
}

/**
 * zif_release_upgrade_version:
 * @release: the #ZifRelease object
 * @version: a distribution version, e.g. 15
 * @upgrade_kind: a #ZifReleaseUpgradeKind, e.g. %ZIF_RELEASE_UPGRADE_KIND_MINIMAL
 * 		  would only download the kernel and initrd, not the stage2 or the packages.
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
zif_release_upgrade_version (ZifRelease *release, guint version, ZifReleaseUpgradeKind upgrade_kind, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	ZifState *state_local;
	GFile *boot_file = NULL;
	GError *error_local = NULL;
	ZifReleasePrivate *priv;
	ZifReleaseUpgradeData *data = NULL;
	ZifMd *md_mirrorlist = NULL;
	gchar *installmirrorlist_filename = NULL;

	g_return_val_if_fail (ZIF_IS_RELEASE (release), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get private */
	priv = release->priv;

	/* junk data for the entire method */
	data = g_new0 (ZifReleaseUpgradeData, 1);
	data->version = version;
	data->upgrade_kind = upgrade_kind;

	/* nothing set */
	if (priv->boot_dir == NULL) {
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_SETUP_INVALID,
				     "no boot dir has been set; use zif_release_set_boot_dir()");
		goto out;
	}

	/* ensure boot directory exists */
	boot_file = g_file_new_for_path (priv->boot_dir);
	ret = g_file_query_exists (boot_file, NULL);
	if (!ret) {
		g_debug ("%s does not exist, creating", priv->boot_dir);
		ret = g_file_make_directory_with_parents (boot_file, NULL, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_WRITE_FAILED,
				     "cannot create boot environment: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* 1. setup
	 * 2. get installmirrorlist
	 * 3. parse installmirrorlist
	 * 4. download treeinfo
	 * 5. download kernel
	 * 6. download initrd
	 * (6) download stage2
	 * (6) download packages
	 * 7. install kernel
	 */
	if (upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_MINIMAL)
		zif_state_set_number_steps (state, 7);
	else if (upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_DEFAULT)
		zif_state_set_number_steps (state, 8);
	else if (upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_COMPLETE)
		zif_state_set_number_steps (state, 9);

	/* get the correct object */
	state_local = zif_state_get_child (state);
	data->upgrade = zif_release_get_upgrade_for_version (release, version, state_local, error);
	if (data->upgrade == NULL)
		goto out;

	/* check size */
	ret = zif_release_check_filesystem_size (priv->boot_dir, 26*1024*1024, error);
	if (!ret)
		goto out;

	/* check size */
	ret = zif_release_check_filesystem_size ("/var/cache", 700*1024*1024, error);
	if (!ret)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get installmirrorlist */
	state_local = zif_state_get_child (state);
	installmirrorlist_filename = g_build_filename (priv->cache_dir, "installmirrorlist", NULL);
	ret = zif_download_file (priv->download,
				 zif_upgrade_get_install_mirrorlist (data->upgrade),
				 installmirrorlist_filename, state_local, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_DOWNLOAD_FAILED,
			     "failed to download installmirrorlist: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* parse the installmirrorlist */
	md_mirrorlist = zif_md_mirrorlist_new ();
	zif_md_set_filename (md_mirrorlist, installmirrorlist_filename);
	zif_md_set_id (md_mirrorlist, "preupgrade-temp");
	state_local = zif_state_get_child (state);
	ret = zif_download_location_add_md (priv->download, md_mirrorlist, state_local, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_DOWNLOAD_FAILED,
			     "failed to add download location installmirrorlist: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* gets .treeinfo */
	state_local = zif_state_get_child (state);
	ret = zif_release_get_treeinfo (release, data, state_local, error);
	if (!ret)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* gets .treeinfo */
	state_local = zif_state_get_child (state);
	ret = zif_release_get_kernel (release, data, state_local, error);
	if (!ret)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* gets .treeinfo */
	state_local = zif_state_get_child (state);
	ret = zif_release_get_initrd (release, data, state_local, error);
	if (!ret)
		goto out;

	/* gets stage2 */
	if (upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_DEFAULT ||
	    upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_COMPLETE) {

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* gets stage2 */
		state_local = zif_state_get_child (state);
		ret = zif_release_get_stage2 (release, data, state_local, error);
		if (!ret)
			goto out;
	}

	/* gets package data */
	if (upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_COMPLETE) {

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* gets stage2 */
		state_local = zif_state_get_child (state);
		ret = zif_release_get_package_data (release, data, state_local, error);
		if (!ret)
			goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* add the new kernel */
	ret = zif_release_add_kernel (release, data, error);
	if (!ret)
		goto out;

	/* make the new kernel default just once */
	ret = zif_release_make_kernel_default_once (release, error);
	if (!ret)
		goto out;

	/* write kickstart */
	ret = zif_release_write_kickstart (release, error);
	if (!ret)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	ret = TRUE;
out:
	/* need to zif_download_location_remove_md or a non-singleton ZifDownload */
	g_free (installmirrorlist_filename);
	if (data != NULL) {
		if (data->upgrade != NULL)
			g_object_unref (data->upgrade);
		if (data->key_file_treeinfo != NULL)
			g_key_file_free (data->key_file_treeinfo);
		g_free (data->images_section);
		g_free (data);
	}
	if (md_mirrorlist != NULL)
		g_object_unref (md_mirrorlist);
	if (boot_file != NULL)
		g_object_unref (boot_file);
	return ret;
}

/**
 * zif_release_set_cache_dir:
 * @release: the #ZifRelease object
 * @cache_dir: the system wide release location, e.g. "/var/cache/PackageKit"
 *
 * Sets the location to use as the local file cache.
 *
 * Since: 0.1.3
 **/
void
zif_release_set_cache_dir (ZifRelease *release, const gchar *cache_dir)
{
	g_return_if_fail (ZIF_IS_RELEASE (release));
	g_return_if_fail (cache_dir != NULL);
	g_return_if_fail (!release->priv->loaded);

	g_free (release->priv->cache_dir);
	release->priv->cache_dir = g_strdup (cache_dir);
}

/**
 * zif_release_set_boot_dir:
 * @release: the #ZifRelease object
 * @boot_dir: the system boot directory, e.g. "/boot/upgrade"
 *
 * Sets the location to use as the boot directory.
 *
 * Since: 0.1.3
 **/
void
zif_release_set_boot_dir (ZifRelease *release, const gchar *boot_dir)
{
	g_return_if_fail (ZIF_IS_RELEASE (release));
	g_return_if_fail (boot_dir != NULL);
	g_return_if_fail (!release->priv->loaded);

	g_free (release->priv->boot_dir);
	release->priv->boot_dir = g_strdup (boot_dir);
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
	g_object_unref (release->priv->config);
	g_free (release->priv->cache_dir);
	g_free (release->priv->boot_dir);
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
	release->priv->config = zif_config_new ();
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

