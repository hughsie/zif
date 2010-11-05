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
	GPtrArray		*array;
	gchar			*cache_dir;
	gchar			*boot_dir;
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
	gchar *filename = NULL;

	/* nothing set */
	if (release->priv->cache_dir == NULL) {
		g_set_error_literal (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
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
			g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
				     "failed to download release info: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* setup watch */
	ret = zif_monitor_add_watch (release->priv->monitor, filename, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
			     "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* open the releases file */
	key_file = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file, filename, 0, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
			     "failed to open release info %s: %s",
			     filename, error_local->message);
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
		     ZIF_RELEASE_ERROR_FAILED,
		     "could not find upgrade version %i", version);
out:
	return upgrade;
}

/**
 * zif_release_add_kernel:
 **/
static gboolean
zif_release_add_kernel (ZifRelease *release, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GString *cmdline = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* linux, TODO: use something else for ppc */
	cmdline = g_string_new ("/sbin/grubby ");
	g_string_append_printf (cmdline,
			        "--make-default ");
	g_string_append_printf (cmdline,
			        "--add-kernel=%s/vmlinuz ", priv->boot_dir);
	g_string_append_printf (cmdline,
			        "--initrd=%s/initrd.img ", priv->boot_dir);
	g_string_append_printf (cmdline,
			        "--title=\"Upgrade to Fedora 15\" ");
	g_string_append_printf (cmdline,
			        "--args=\"preupgrade stage2=%s/stage2.img otherargs=fubar\"", priv->boot_dir);

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
		g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
			     "failed to add kernel: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	if (cmdline != NULL)
		g_string_free (cmdline, TRUE);
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
			     ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
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
pk_release_checksum_matches_file (const gchar *filename, const gchar *sha256, GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	gsize len;
	const gchar *got;
	GChecksum *checksum = NULL;

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
	g_free (data);
	if (checksum != NULL)
		g_checksum_free (checksum);
	return ret;
}

/**
 * zif_release_get_kernel_and_initrd:
 **/
static gboolean
zif_release_get_kernel_and_initrd (ZifRelease *release, guint version, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *treeinfo_uri = NULL;
	ZifConfig *config = NULL;
	gchar *basearch = NULL;
	gchar *images_section = NULL;
	gchar *kernel = NULL;
	gchar *kernel_checksum = NULL;
	gchar *kernel_filename = NULL;
	gchar *initrd = NULL;
	gchar *initrd_checksum = NULL;
	gchar *initrd_filename = NULL;
	gchar *stage2 = NULL;
	gchar *stage2_checksum = NULL;
	gchar *treeinfo_filename = NULL;
	GKeyFile *key_file_treeinfo = NULL;
	gint version_tmp;
	ZifState *state_local;
	ZifReleasePrivate *priv = release->priv;

	/* 1. get treeinfo
	 * 2. get kernel
	 * 3. get initrd
	 */
	zif_state_set_number_steps (state, 3);

	/* get .treeinfo from a mirror in the installmirrorlist */
	treeinfo_filename = g_build_filename (priv->cache_dir, ".treeinfo", NULL);
	ret = g_file_test (treeinfo_filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		state_local = zif_state_get_child (state);
		ret = zif_download_location (priv->download, ".treeinfo", treeinfo_filename, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "failed to download treeinfo: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* parse the treeinfo file */
	key_file_treeinfo = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file_treeinfo, treeinfo_filename, 0, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "failed to open treeinfo: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* verify the version is sane */
	version_tmp = g_key_file_get_integer (key_file_treeinfo, "general", "version", NULL);
	if (version_tmp != (gint) version) {
		g_set_error_literal (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "treeinfo release differs from wanted release");
		goto out;
	}

	/* get the correct section */
	config = zif_config_new ();
	basearch = zif_config_get_string (config, "basearch", NULL);
	if (basearch == NULL) {
		g_set_error_literal (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "failed to get basearch");
		goto out;
	}
	images_section = g_strdup_printf ("images-%s", basearch);

	/* download the kernel, initrd and stage2 */
	kernel = g_key_file_get_string (key_file_treeinfo, images_section, "kernel", NULL);
	if (kernel == NULL) {
		g_set_error_literal (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "failed to get kernel section");
		goto out;
	}
	initrd = g_key_file_get_string (key_file_treeinfo, images_section, "initrd", NULL);
	if (initrd == NULL) {
		g_set_error_literal (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "failed to get initrd section");
		goto out;
	}
	stage2 = g_key_file_get_string (key_file_treeinfo, "stage2", "mainimage", NULL);
	if (stage2 == NULL) {
		g_set_error_literal (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "failed to get stage2 section");
		goto out;
	}
	kernel_checksum = g_key_file_get_string (key_file_treeinfo, "checksums", kernel, NULL);
	initrd_checksum = g_key_file_get_string (key_file_treeinfo, "checksums", initrd, NULL);
	stage2_checksum = g_key_file_get_string (key_file_treeinfo, "checksums", stage2, NULL);

	/* does kernel already exist */
	kernel_filename = g_build_filename (priv->boot_dir, "vmlinuz", NULL);
	ret = g_file_test (kernel_filename, G_FILE_TEST_EXISTS);

	/* check the checksum matches */
	if (ret) {
		ret = pk_release_checksum_matches_file (kernel_filename, kernel_checksum, &error_local);
		if (!ret) {
			g_debug ("failed kernel checksum: %s", error_local->message);
			/* not fatal */
			g_clear_error (&error_local);
			g_unlink (kernel_filename);
		} else {
			g_debug ("%s already exists and is correct", kernel_filename);
		}
	}

	/* download kernel */
	if (!ret) {
		state_local = zif_state_get_child (state);
		ret = zif_download_location_full (priv->download, kernel, kernel_filename,
						  0, "text/plain",
						  G_CHECKSUM_SHA256, kernel_checksum+7,
						  state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "failed to download kernel: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* check downloaded kernel matches its checksum */
	ret = pk_release_checksum_matches_file (kernel_filename, kernel_checksum+7, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "failed kernel checksum: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* does initrd already exist */
	initrd_filename = g_build_filename (priv->boot_dir, "initrd.img", NULL);
	ret = g_file_test (initrd_filename, G_FILE_TEST_EXISTS);

	/* check the checksum matches */
	if (ret) {
		ret = pk_release_checksum_matches_file (initrd_filename, initrd_checksum+7, &error_local);
		if (!ret) {
			g_debug ("failed initrd checksum: %s", error_local->message);
			/* not fatal */
			g_clear_error (&error_local);
			g_unlink (initrd_filename);
		} else {
			g_debug ("%s already exists and is correct", initrd_filename);
		}
	}

	/* download initrd */
	if (!ret) {
		state_local = zif_state_get_child (state);
		ret = zif_download_location_full (priv->download, initrd,
						  initrd_filename,
						  0, "application/x-extension-img",
						  G_CHECKSUM_SHA256, initrd_checksum+7,
						  state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED, "failed to download kernel: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_free (stage2);
	g_free (stage2_checksum);
	g_free (kernel);
	g_free (kernel_checksum);
	g_free (kernel_filename);
	g_free (initrd);
	g_free (initrd_checksum);
	g_free (initrd_filename);
	g_free (images_section);
	g_free (basearch);
	g_free (treeinfo_filename);
	if (config != NULL)
		g_object_unref (config);
	if (key_file_treeinfo != NULL)
		g_key_file_free (key_file_treeinfo);
	g_free (treeinfo_uri);
	return ret;
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
	ZifUpgrade *upgrade = NULL;
	ZifState *state_local;
	GError *error_local = NULL;
	ZifReleasePrivate *priv;
	ZifMd *md_mirrorlist = NULL;
	gchar *installmirrorlist_filename = NULL;

	g_return_val_if_fail (ZIF_IS_RELEASE (release), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get private */
	priv = release->priv;

	/* nothing set */
	if (priv->boot_dir == NULL) {
		g_set_error_literal (error, ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
				     "no boot dir has been set; use zif_release_set_boot_dir()");
		goto out;
	}

	/* 1. setup
	 * 2. get installmirrorlist
	 * 3. parse installmirrorlist
	 * 4. download kernel and initrd
	 * 5. install kernel
	 */
	zif_state_set_number_steps (state, 5);

	/* get the correct object */
	state_local = zif_state_get_child (state);
	upgrade = zif_release_get_upgrade_for_version (release, version, state_local, error);
	if (upgrade == NULL)
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
				 zif_upgrade_get_install_mirrorlist (upgrade),
				 installmirrorlist_filename, state_local, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
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
			     ZIF_RELEASE_ERROR, ZIF_RELEASE_ERROR_FAILED,
			     "failed to add download location installmirrorlist: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* gets treeinfo, kernel and initrd */
	state_local = zif_state_get_child (state);
	ret = zif_release_get_kernel_and_initrd (release, version, state_local, error);
	if (!ret)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* add the new kernel */
	ret = zif_release_add_kernel (release, error);
	if (!ret)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	ret = TRUE;
out:
	g_free (installmirrorlist_filename);
	if (md_mirrorlist != NULL)
		g_object_unref (md_mirrorlist);
	if (upgrade != NULL)
		g_object_unref (upgrade);
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

