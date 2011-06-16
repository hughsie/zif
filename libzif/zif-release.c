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
 * @short_description: Check for distribution upgrades
 *
 * #ZifRelease allows the user to check for distribution upgrades and
 * upgrade to the newest release.
 *
 * Before checking for upgrades, the releases release file has to be set
 * using the config file and any checks prior to that will fail.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "zif-config.h"
#include "zif-download.h"
#include "zif-md-mirrorlist.h"
#include "zif-monitor.h"
#include "zif-package-remote.h"
#include "zif-release.h"
#include "zif-repos.h"
#include "zif-store-array.h"

#define ZIF_RELEASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_RELEASE, ZifReleasePrivate))

struct _ZifReleasePrivate
{
	gboolean		 loaded;
	ZifMonitor		*monitor;
	ZifDownload		*download;
	ZifConfig		*config;
	GPtrArray		*array;
	guint			 monitor_changed_id;
};

/* only used when doing an upgrade */
typedef struct {
	ZifUpgrade		*upgrade;
	ZifReleaseUpgradeKind	 upgrade_kind;
	guint			 version;
	GKeyFile		*key_file_treeinfo;
	gchar			*uuid_root;
	gchar			*uuid_boot;
	gchar			*images_section;
	gboolean		 has_stage2;
} ZifReleaseUpgradeData;

G_DEFINE_TYPE (ZifRelease, zif_release, G_TYPE_OBJECT)

/**
 * zif_release_error_quark:
 *
 * Return value: An error quark.
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
 * zif_release_get_file_age:
 **/
static guint64
zif_release_get_file_age (GFile *file, GError **error)
{
	GFileInfo *file_info;
	guint64 modified;
	guint64 age = G_MAXUINT64;

	/* get file attributes */
	file_info = g_file_query_info (file,
				       G_FILE_ATTRIBUTE_TIME_MODIFIED,
				       G_FILE_QUERY_INFO_NONE, NULL,
				       error);
	if (file_info == NULL)
		goto out;

	/* get age */
	modified = g_file_info_get_attribute_uint64 (file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	age = time (NULL) - modified;
out:
	if (file_info != NULL)
		g_object_unref (file_info);
	return age;
}

/**
 * zif_release_load:
 **/
static gboolean
zif_release_load (ZifRelease *release, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	gchar *cache_dir = NULL;
	gchar *filename = NULL;
	gchar **groups = NULL;
	gchar *temp;
	gchar *temp_expand;
	gchar *uri = NULL;
	GError *error_local = NULL;
	GFile *cache_dir_file = NULL;
	GFile *file = NULL;
	GKeyFile *key_file = NULL;
	guint64 cache_age, age;
	guint i;
	ZifReleasePrivate *priv = release->priv;
	ZifUpgrade *upgrade;

	/* nothing set */
	cache_dir = zif_config_get_string (priv->config,
					   "upgrade_cache_dir",
					   error);
	if (cache_dir == NULL)
		goto out;

	/* download if it does not already exist */
	filename = g_build_filename (cache_dir, "releases.txt", NULL);
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (ret) {
		/* check file age */
		file = g_file_new_for_path (filename);
		age = zif_release_get_file_age (file, &error_local);
		if (age == G_MAXUINT64) {
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_SETUP_INVALID,
				     "failed to get age for release info: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* delete it if it's older */
		cache_age = zif_config_get_uint (priv->config, "metadata_expire", NULL);
		if (age > cache_age) {
			g_debug ("deleting old %s as too old", filename);
			ret = g_file_delete (file, NULL, &error_local);
			if (!ret) {
				g_set_error (error,
					     ZIF_RELEASE_ERROR,
					     ZIF_RELEASE_ERROR_SETUP_INVALID,
					     "failed to delete old releases file: %s",
					     error_local->message);
				g_error_free (error_local);
				goto out;
			}

			/* force download as if this never existed */
			ret = FALSE;
		}
	}
	if (!ret) {
		uri = zif_config_get_string (priv->config,
					     "upgrade_releases_uri",
					     error);
		if (uri == NULL) {
			ret = FALSE;
			goto out;
		}

		/* make directory if it does not exist */
		cache_dir_file = g_file_new_for_path (cache_dir);
		ret = g_file_query_exists (cache_dir_file, NULL);
		if (!ret) {
			g_debug ("creating missing cache dir '%s'",
				 cache_dir);
			ret = g_file_make_directory_with_parents (cache_dir_file, NULL, error);
			if (!ret)
				goto out;
		}

		/* download file */
		ret = zif_download_file (priv->download,
					 uri,
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
	ret = zif_monitor_add_watch (priv->monitor, filename, &error_local);
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
		g_free (temp);
		temp = g_key_file_get_string (key_file, groups[i], "preupgrade-ok", NULL);
		if (g_strcmp0 (temp, "True") == 0)
			zif_upgrade_set_enabled (upgrade, TRUE);
		g_free (temp);
		zif_upgrade_set_version (upgrade, g_key_file_get_integer (key_file, groups[i], "version", NULL));
		temp = g_key_file_get_string (key_file, groups[i], "baseurl", NULL);
		if (temp != NULL) {
			temp_expand = zif_config_expand_substitutions (priv->config, temp, NULL);
			zif_upgrade_set_baseurl (upgrade, temp_expand);
			g_free (temp_expand);
			g_free (temp);
		}
		temp = g_key_file_get_string (key_file, groups[i], "mirrorlist", NULL);
		if (temp != NULL) {
			temp_expand = zif_config_expand_substitutions (priv->config, temp, NULL);
			zif_upgrade_set_mirrorlist (upgrade, temp_expand);
			g_free (temp_expand);
			g_free (temp);
		}
		temp = g_key_file_get_string (key_file, groups[i], "installmirrorlist", NULL);
		if (temp != NULL) {
			temp_expand = zif_config_expand_substitutions (priv->config, temp, NULL);
			zif_upgrade_set_install_mirrorlist (upgrade, temp_expand);
			g_free (temp_expand);
			g_free (temp);
		}
		g_ptr_array_add (priv->array, upgrade);
	}

	/* done */
	priv->loaded = TRUE;
out:
	g_free (cache_dir);
	g_free (uri);
	g_free (filename);
	g_strfreev (groups);
	if (key_file != NULL)
		g_key_file_free (key_file);
	if (file != NULL)
		g_object_unref (file);
	if (cache_dir_file != NULL)
		g_object_unref (cache_dir_file);
	return ret;
}

/**
 * zif_release_get_upgrades:
 * @release: A #ZifRelease
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets all the upgrades, older and newer.
 *
 * Return value: An array of #ZifUpgrade's, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_release_get_upgrades (ZifRelease *release,
			  ZifState *state,
			  GError **error)
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
 * @release: A #ZifRelease
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets all the upgrades older than the one currently installed.
 *
 * Return value: An array of #ZifUpgrade's, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_release_get_upgrades_new (ZifRelease *release,
			      ZifState *state,
			      GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	guint i;
	guint version;
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

	/* get current version */
	version = zif_config_get_uint (release->priv->config,
				       "releasever", error);
	if (version == G_MAXUINT)
		goto out;

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
 * @release: A #ZifRelease
 * @version: The distribution version, e.g. 15
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets a specific upgrade object for the given version.
 *
 * Return value: A #ZifUpgrade, free with g_object_unref()
 *
 * Since: 0.1.3
 **/
ZifUpgrade *
zif_release_get_upgrade_for_version (ZifRelease *release,
				     guint version,
				     ZifState *state,
				     GError **error)
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
 * zif_release_remove_kernel:
 **/
static gboolean
zif_release_remove_kernel (ZifRelease *release,
			   ZifReleaseUpgradeData *data,
			   GError **error)
{
	gboolean ret;
	gchar *boot_dir = NULL;
	gchar *cmdline = NULL;
	GError *error_local = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* we're not running as root */
	boot_dir = zif_config_get_string (priv->config,
					  "upgrade_boot_dir",
					  error);
	if (boot_dir == NULL) {
		ret = FALSE;
		goto out;
	}
	cmdline = g_strdup_printf ("/sbin/grubby "
				   "--config-file=/boot/grub/grub.conf "
				   "--remove-kernel=%s/vmlinuz",
				   boot_dir);
	if (!g_str_has_prefix (boot_dir, "/boot")) {
		g_debug ("not running grubby as not installing root, would have run '%s'",
			 cmdline);
		ret = TRUE;
		goto out;
	}

	/* run the command */
	g_debug ("running command %s", cmdline);
	ret = g_spawn_command_line_sync (cmdline, NULL, NULL, NULL, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_SPAWN_FAILED,
			     "failed to add kernel: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (boot_dir);
	g_free (cmdline);
	return ret;
}

/**
 * zif_release_add_kernel:
 **/
static gboolean
zif_release_add_kernel (ZifRelease *release,
			ZifReleaseUpgradeData *data,
			GError **error)
{
	gboolean ret;
	gchar *arch = NULL;
	gchar *boot_dir = NULL;
	gchar *repo_dir = NULL;
	gchar *title = NULL;
	GError *error_local = NULL;
	GString *args = NULL;
	GString *cmdline = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* yaboot (ppc) doesn't support spaces in titles */
	arch = zif_config_get_string (priv->config, "basearch", NULL);
	if (g_str_has_prefix (arch, "ppc")) {
		title = g_strdup ("upgrade");
	} else {
		title = g_strdup_printf ("Upgrade to Fedora %i",
					 zif_upgrade_get_version (data->upgrade));
	}

	/* write kickstart info */
	args = g_string_new ("preupgrade ");
	g_string_append_printf (args,
			        "ks=hd:UUID=%s:/upgrade/ks.cfg ",
			        data->uuid_boot);

	/* kernel arguments */
	if (data->has_stage2) {
		g_string_append_printf (args,
					"stage2=hd:UUID=%s:/upgrade/install.img ",
					data->uuid_boot);
	}
	if (data->upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_COMPLETE) {
		repo_dir = zif_config_get_string (priv->config,
						  "upgrade_repo_dir",
						  error);
		if (repo_dir == NULL) {
			ret = FALSE;
			goto out;
		}
		g_string_append_printf (args, "repo=hd::%s ", repo_dir);
	}
	g_string_append (args,
			 "ksdevice=link ip=dhcp ipv6=dhcp ");

	/* get bootdir */
	boot_dir = zif_config_get_string (priv->config,
					  "upgrade_boot_dir",
					  error);
	if (boot_dir == NULL) {
		ret = FALSE;
		goto out;
	}

	/* do for i386 and ppc */
	cmdline = g_string_new ("/sbin/grubby ");
	g_string_append (cmdline,
			 "--config-file=/boot/grub/grub.conf ");
	g_string_append_printf (cmdline,
			        "--add-kernel=%s/vmlinuz ", boot_dir);
	g_string_append_printf (cmdline,
			        "--initrd=%s/initrd.img ", boot_dir);
	g_string_append_printf (cmdline,
			        "--title=\"%s\" ", title);
	g_string_append_printf (cmdline,
			        "--args=\"%s\"", args->str);

	/* we're not running as root */
	if (!g_str_has_prefix (boot_dir, "/boot")) {
		g_debug ("not running grubby as not installing root, would have run '%s'",
			 cmdline->str);
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
	g_free (boot_dir);
	g_free (repo_dir);
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
	gchar *boot_dir = NULL;
	gchar *cmdline = NULL;
	GError *error_local = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* get bootdir */
	boot_dir = zif_config_get_string (priv->config,
					  "upgrade_boot_dir",
					  error);
	if (boot_dir == NULL) {
		ret = FALSE;
		goto out;
	}

	/* We want to run something like:
	 *
	 * /bin/echo 'savedefault --default=0 --once' | /sbin/grub > /dev/null
	 *
	 * ...but this won't work in C and is a bodge.
	 * Ideally we want to add --once to the list of grubby commands. */
	cmdline = g_strdup_printf ("/sbin/grubby "
				   "--config-file=/boot/grub/grub.conf "
				   "--set-default=%s/vmlinuz",
				   boot_dir);

	/* we're not running as root */
	if (!g_str_has_prefix (boot_dir, "/boot")) {
		g_debug ("not running grub as not installing root, would have run '%s'",
			 cmdline);
		goto out;
	}
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
	g_free (boot_dir);
	g_free (cmdline);
	return ret;
}

/**
 * zif_release_check_filesystem_size:
 **/
static gboolean
zif_release_check_filesystem_size (const gchar *location,
				   guint64 required_size,
				   GError **error)
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
pk_release_checksum_matches_file (const gchar *filename,
				  const gchar *sha256,
				  ZifState *state,
				  GError **error)
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
zif_release_get_treeinfo (ZifRelease *release,
			  ZifReleaseUpgradeData *data,
			  ZifState *state,
			  GError **error)
{
	gboolean ret;
	gchar *basearch = NULL;
	gchar *cache_dir = NULL;
	gchar *treeinfo_filename = NULL;
	gchar *treeinfo_uri = NULL;
	GError *error_local = NULL;
	gint version_tmp;
	ZifReleasePrivate *priv = release->priv;
	ZifState *state_local;

	/* set steps */
	ret = zif_state_set_steps (state,
				   error,
				   90, /* get treeinfo */
				   10, /* parse it */
				   -1);
	if (!ret)
		goto out;

	/* get .treeinfo from a mirror in the installmirrorlist */
	cache_dir = zif_config_get_string (priv->config,
					   "upgrade_cache_dir",
					   error);
	if (cache_dir == NULL) {
		ret = FALSE;
		goto out;
	}
	treeinfo_filename = g_build_filename (cache_dir, ".treeinfo", NULL);
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
	ret = g_key_file_load_from_file (data->key_file_treeinfo,
					 treeinfo_filename,
					 0,
					 &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_FILE_INVALID,
			     "failed to open treeinfo: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* verify the version is sane */
	version_tmp = g_key_file_get_integer (data->key_file_treeinfo,
					      "general",
					      "version",
					      NULL);
	if (version_tmp != (gint) data->version) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_FILE_INVALID,
			     "treeinfo release '%i' differs from wanted release '%i'",
			     version_tmp, data->version);
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
	g_free (cache_dir);
	g_free (basearch);
	g_free (treeinfo_filename);
	g_free (treeinfo_uri);
	return ret;
}

/**
 * zif_release_get_kernel:
 **/
static gboolean
zif_release_get_kernel (ZifRelease *release,
			ZifReleaseUpgradeData *data,
			ZifState *state,
			GError **error)
{
	gboolean ret = FALSE;
	gchar *boot_dir = NULL;
	gchar *checksum = NULL;
	gchar *filename = NULL;
	gchar *kernel = NULL;
	GError *error_local = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* get data */
	kernel = g_key_file_get_string (data->key_file_treeinfo,
					data->images_section,
					"kernel",
					NULL);
	if (kernel == NULL) {
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_FILE_INVALID,
				     "failed to get kernel section");
		goto out;
	}
	checksum = g_key_file_get_string (data->key_file_treeinfo,
					  "checksums",
					  kernel,
					  NULL);

	/* check the checksum matches */
	boot_dir = zif_config_get_string (priv->config,
					  "upgrade_boot_dir",
					  error);
	if (boot_dir == NULL) {
		ret = FALSE;
		goto out;
	}
	filename = g_build_filename (boot_dir, "vmlinuz", NULL);
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
	g_free (boot_dir);
	g_free (kernel);
	g_free (checksum);
	g_free (filename);
	return ret;
}

/**
 * zif_release_get_initrd:
 **/
static gboolean
zif_release_get_initrd (ZifRelease *release,
			ZifReleaseUpgradeData *data,
			ZifState *state,
			GError **error)
{
	gboolean ret = FALSE;
	gchar *boot_dir = NULL;
	gchar *checksum = NULL;
	gchar *filename = NULL;
	gchar *initrd = NULL;
	GError *error_local = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* get data */
	initrd = g_key_file_get_string (data->key_file_treeinfo,
					data->images_section,
					"initrd",
					NULL);
	if (initrd == NULL) {
		g_set_error_literal (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_FILE_INVALID,
				     "failed to get initrd section");
		goto out;
	}
	checksum = g_key_file_get_string (data->key_file_treeinfo,
					  "checksums",
					  initrd,
					  NULL);

	/* check the checksum matches */
	boot_dir = zif_config_get_string (priv->config,
					  "upgrade_boot_dir",
					  error);
	if (boot_dir == NULL) {
		ret = FALSE;
		goto out;
	}
	filename = g_build_filename (boot_dir, "initrd.img", NULL);
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
						  0,
						  "application/x-gzip,"
						  "application/x-extension-img,"
						  "application/x-xz",
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
	g_free (boot_dir);
	g_free (initrd);
	g_free (checksum);
	g_free (filename);
	return ret;
}

/**
 * zif_release_get_stage2:
 **/
static gboolean
zif_release_get_stage2 (ZifRelease *release,
			ZifReleaseUpgradeData *data,
			ZifState *state,
			GError **error)
{
	gboolean ret = FALSE;
	gchar *boot_dir = NULL;
	gchar *checksum = NULL;
	gchar *filename = NULL;
	gchar *stage2 = NULL;
	GError *error_local = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* get data */
	stage2 = g_key_file_get_string (data->key_file_treeinfo, "stage2", "mainimage", NULL);
	if (stage2 == NULL) {
		/* distros from F15+ do not ship a seporate stage2 image */
		g_debug ("failed to get stage2 section as nothing was specified");
		ret = TRUE;
		goto out;
	}
	checksum = g_key_file_get_string (data->key_file_treeinfo, "checksums", stage2, NULL);

	/* check the checksum matches */
	boot_dir = zif_config_get_string (priv->config,
					  "upgrade_boot_dir",
					  error);
	if (boot_dir == NULL) {
		ret = FALSE;
		goto out;
	}
	filename = g_build_filename (boot_dir, "install.img", NULL);
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
						  0,  "application/x-extension-img,application/octet-stream",
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

	/* got valid stage2 image */
	data->has_stage2 = TRUE;
out:
	g_free (boot_dir);
	g_free (filename);
	g_free (stage2);
	g_free (checksum);
	return ret;
}

/**
 * zif_release_get_keyfile_value:
 **/
static gchar *
zif_release_get_keyfile_value (const gchar *filename,
			       const gchar *key)
{
	GFile *file;
	gchar *data = NULL;
	gchar *value = NULL;
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
			value = g_strdup (lines[i]+len+2);
			g_strdelimit (value, "\"", '\0');
			break;
		}
	}
out:
	g_free (data);
	g_object_unref (file);
	g_strfreev (lines);
	return value;
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

	/* cleanup */
	g_strdelimit (uuid, "\n", '\0');
out:
	g_free (cmdline);
	return uuid;
}

/**
 * zif_release_write_kickstart:
 **/
static gboolean
zif_release_write_kickstart (ZifRelease *release, ZifReleaseUpgradeData *data, GError **error)
{
	gboolean ret = FALSE;
	gchar *boot_dir = NULL;
	gchar *keymap = NULL;
	gchar *ks_filename = NULL;
	gchar *lang = NULL;
	gchar *repo_dir = NULL;
	GError *error_local = NULL;
	GFile *ks_file = NULL;
	GString *string = NULL;
	ZifReleasePrivate *priv = release->priv;

	/* get bootdir */
	boot_dir = zif_config_get_string (priv->config,
					  "upgrade_boot_dir",
					  error);
	if (boot_dir == NULL) {
		ret = FALSE;
		goto out;
	}
	ks_filename = g_build_filename (boot_dir, "ks.cfg", NULL);
	ks_file = g_file_new_for_path (ks_filename);
	string = g_string_new ("# ks.cfg generated by Zif\n");

	/* get system defaults */
	lang = zif_release_get_lang ();
	keymap = zif_release_get_keymap ();

	/* get repodir */
	repo_dir = zif_config_get_string (priv->config,
					  "upgrade_repo_dir",
					  error);
	if (repo_dir == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get kickstart */
	g_string_append_printf (string, "lang %s\n", lang);
	g_string_append_printf (string, "keyboard %s\n", keymap);
	g_string_append (string, "bootloader --upgrade --location=none\n");
	g_string_append (string, "clearpart --none\n");
	g_string_append_printf (string, "upgrade --root-device=UUID=%s\n", data->uuid_root);
	g_string_append (string, "reboot\n");
	g_string_append (string, "\n");
	g_string_append (string, "%post\n");
	g_string_append_printf (string, "grubby --remove-kernel=%s/vmlinuz\n", boot_dir);
	g_string_append_printf (string, "rm -rf %s %s*\n", boot_dir, repo_dir);
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
	g_free (boot_dir);
	g_free (repo_dir);
	g_free (lang);
	g_free (keymap);
	g_object_unref (ks_file);
	if (string != NULL)
		g_string_free (string, TRUE);
	g_free (ks_filename);
	return ret;
}

/**
 * zif_release_get_package_data:
 **/
static gboolean
zif_release_get_package_data (ZifRelease *release,
			      ZifReleaseUpgradeData *data,
			      ZifState *state,
			      GError **error)
{
	gboolean ret;
	GCancellable *cancellable;
	gchar *cmdline = NULL;
	gchar *cmdline2 = NULL;
	gchar *repo_dir = NULL;
	gchar *repo_packages = NULL;
	gchar *repo_metadata = NULL;
	GError *error_local = NULL;
	GFile *file = NULL;
	GPtrArray *array = NULL;
	GPtrArray *updates = NULL;
	guint i;
	guint old_release;
	ZifMd *md_tmp;
	ZifPackage *package;
	ZifRepos *repos = NULL;
	ZifState *state_local;
	ZifState *state_loop;
	ZifStoreRemote *store;
	ZifReleasePrivate *priv = release->priv;

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (state,
				   error,
				   5, /* setup directory */
				   1, /* get local stores */
				   5, /* refresh each repo */
				   5, /* get updates */
				   75, /* download files */
				   5, /* createrepo */
				   2, /* get comps data */
				   2, /* modify repo */
				   -1);
	if (!ret)
		goto out;

	/* create directory path */
	repo_dir = zif_config_get_string (priv->config,
					  "upgrade_repo_dir",
					  error);
	if (repo_dir == NULL) {
		ret = FALSE;
		goto out;
	}
	file = g_file_new_for_path (repo_dir);
	cancellable = zif_state_get_cancellable (state);
	ret = g_file_query_exists (file, cancellable);
	if (!ret) {
		ret = g_file_make_directory_with_parents (file, cancellable, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_SETUP_INVALID,
				     "failed to create repo: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* override the release version */
	old_release = zif_config_get_uint (priv->config,
					   "releasever",
					   error);
	if (old_release == G_MAXUINT) {
		ret = FALSE;
		goto out;
	}
	ret = zif_config_unset (priv->config,
				"releasever",
				error);
	if (!ret)
		goto out;
	ret = zif_config_set_uint (priv->config,
				   "releasever",
				   data->version,
				   error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get the list of currently enabled repos */
	repos = zif_repos_new ();
	state_local = zif_state_get_child (state);
	array = zif_repos_get_stores_enabled (repos, state_local, error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* refresh each repo */
	state_local = zif_state_get_child (state);
	ret = zif_store_array_refresh (array, FALSE, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get the list of updates */
	state_local = zif_state_get_child (state);
	updates = zif_store_array_get_updates (array, state_local, error);
	if (updates == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* set number of download files */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, updates->len);

	/* download each update to /var/cache/preupgrade/packages*/
	repo_packages = g_build_filename (repo_dir, "packages", NULL);
	for (i=0; i<updates->len; i++) {
		package = g_ptr_array_index (updates, i);
		g_debug ("download %s", zif_package_get_printable (package));
		state_loop = zif_state_get_child (state_local);
		ret = zif_package_remote_download (ZIF_PACKAGE_REMOTE (package),
						   repo_packages,
						   state_loop,
						   error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* TODO: maybe do a test transaction */

	/* create the repodata */
	cmdline = g_strdup_printf ("/usr/bin/createrepo --database %s", repo_dir);
	g_debug ("running command %s", cmdline);
	ret = g_spawn_command_line_sync (cmdline, NULL, NULL, NULL, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_SPAWN_FAILED,
			     "failed to create the repo: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* add the comps group data */
	state_local = zif_state_get_child (state);
	store = zif_repos_get_store (repos, "updates", state_local, error);
	if (store == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get the correct metadata */
	md_tmp = zif_store_remote_get_md_from_type (store,
						    ZIF_MD_KIND_COMPS_GZ);
	if (md_tmp == NULL) {
		md_tmp = zif_store_remote_get_md_from_type (store,
							    ZIF_MD_KIND_COMPS_GZ);
	}
	if (md_tmp == NULL) {
		ret = FALSE;
		goto out;
	}

	/* create the repodata */
	repo_metadata = g_build_filename (repo_dir, "repodata", NULL);
	cmdline2 = g_strdup_printf ("/usr/bin/modifyrepo --mdtype=group_gz %s %s",
				    zif_md_get_filename (md_tmp),
				    repo_metadata);
	g_debug ("running command %s", cmdline2);
	ret = g_spawn_command_line_sync (cmdline2, NULL, NULL, NULL, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_SPAWN_FAILED,
			     "failed to create the repo: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* reset the release version */
	ret = zif_config_unset (priv->config,
				"releasever",
				error);
	if (!ret)
		goto out;
	ret = zif_config_set_uint (priv->config,
				   "releasever",
				   old_release,
				   error);
	if (!ret)
		goto out;
out:
	g_free (repo_dir);
	g_free (repo_packages);
	g_free (repo_metadata);
	g_free (cmdline);
	g_free (cmdline2);
	if (file != NULL)
		g_object_unref (file);
	if (repos != NULL)
		g_object_unref (repos);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (updates != NULL)
		g_ptr_array_unref (updates);
	return ret;
}

/**
 * zif_release_get_mtab_entry:
 **/
static gchar *
zif_release_get_mtab_entry (const gchar *mount_point, GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	gchar *device = NULL;
	gchar **lines = NULL;
	gchar **split;
	guint i;

	/* get mtab contents */
	ret = g_file_get_contents ("/etc/mtab", &data, NULL, error);
	if (!ret)
		goto out;

	/* find the mountpoint, and get the device name */
	lines = g_strsplit (data, "\n", -1);
	for (i=0; lines[i] != NULL && device == NULL; i++) {
		split = g_strsplit (lines[i], " ", -1);
		if (g_strcmp0 (split[1], mount_point) == 0)
			device = g_strdup (split[0]);
		g_strfreev (split);
	}

	/* nothing found */
	if (device == NULL) {
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_NOT_SUPPORTED,
			     "no mtab entry for %s", mount_point);
	}
out:
	g_strfreev (lines);
	g_free (data);
	return device;
}

/**
 * zif_release_upgrade_version:
 * @release: A #ZifRelease
 * @version: A distribution version, e.g. 15
 * @upgrade_kind: The kind of upgrade to perform, e.g. %ZIF_RELEASE_UPGRADE_KIND_MINIMAL
 * 		  would only download the kernel and initrd, not the stage2 or the packages.
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Upgrade the distribution to a given version.
 *
 * Return value: A #ZifUpgrade, free with g_object_unref()
 *
 * Since: 0.1.3
 **/
gboolean
zif_release_upgrade_version (ZifRelease *release,
			     guint version,
			     ZifReleaseUpgradeKind upgrade_kind,
			     ZifState *state,
			     GError **error)
{
	gboolean ret = FALSE;
	gchar *boot_dir = NULL;
	gchar *cache_dir = NULL;
	gchar *installmirrorlist_filename = NULL;
	gchar *mtab_entry = NULL;
	GError *error_local = NULL;
	GFile *boot_file = NULL;
	ZifMd *md_mirrorlist = NULL;
	ZifReleasePrivate *priv;
	ZifReleaseUpgradeData *data = NULL;
	ZifState *state_local;

	g_return_val_if_fail (ZIF_IS_RELEASE (release), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get private */
	priv = release->priv;

	/* junk data for the entire method */
	data = g_new0 (ZifReleaseUpgradeData, 1);
	data->version = version;
	data->upgrade_kind = upgrade_kind;

	/* ensure boot directory exists */
	boot_dir = zif_config_get_string (priv->config,
					  "upgrade_boot_dir",
					  error);
	if (boot_dir == NULL) {
		ret = FALSE;
		goto out;
	}
	boot_file = g_file_new_for_path (boot_dir);
	ret = g_file_query_exists (boot_file, NULL);
	if (!ret) {
		g_debug ("%s does not exist, creating", boot_dir);
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

	/* setup steps */
	if (upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_MINIMAL) {
		ret = zif_state_set_steps (state,
					   error,
					   1, /* setup */
					   5, /* get installmirrorlist */
					   1, /* parse installmirrorlist */
					   3, /* download treeinfo */
					   15, /* download kernel */
					   70, /* download initrd */
					   5, /* install kernel */
					   -1);
	} else if (upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_DEFAULT) {
		ret = zif_state_set_steps (state,
					   error,
					   1, /* setup */
					   5, /* get installmirrorlist */
					   1, /* parse installmirrorlist */
					   3, /* download treeinfo */
					   15, /* download kernel */
					   20, /* download initrd */
					   50, /* download stage2 */
					   5, /* install kernel */
					   -1);
	} else if (upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_COMPLETE) {
		ret = zif_state_set_steps (state,
					   error,
					   1, /* setup */
					   5, /* get installmirrorlist */
					   1, /* parse installmirrorlist */
					   3, /* download treeinfo */
					   5, /* download kernel */
					   20, /* download initrd */
					   30, /* download stage2 */
					   30, /* download packages */
					   5, /* install kernel */
					   -1);
	}
	if (!ret)
		goto out;

	/* get the correct object */
	state_local = zif_state_get_child (state);
	data->upgrade = zif_release_get_upgrade_for_version (release, version, state_local, error);
	if (data->upgrade == NULL)
		goto out;

	/* check size */
	ret = zif_release_check_filesystem_size (boot_dir, 26*1024*1024, error);
	if (!ret)
		goto out;

	/* check size */
	ret = zif_release_check_filesystem_size ("/var/cache", 700*1024*1024, error);
	if (!ret)
		goto out;

	/* get uuids */
	data->uuid_root = zif_release_get_uuid ("/dev/root", &error_local);
	if (data->uuid_root == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_RELEASE_ERROR,
			     ZIF_RELEASE_ERROR_NO_UUID_FOR_ROOT,
			     "failed to get uuid for root: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the boot uuid */
	mtab_entry = zif_release_get_mtab_entry ("/boot", &error_local);
	if (mtab_entry == NULL) {
		g_debug ("using root uuid: %s", error_local->message);
		data->uuid_boot = g_strdup (data->uuid_root);
		g_clear_error (&error_local);
	} else {
		data->uuid_boot = zif_release_get_uuid (mtab_entry, &error_local);
		if (data->uuid_boot == NULL) {
			ret = FALSE;
			g_set_error (error,
				     ZIF_RELEASE_ERROR,
				     ZIF_RELEASE_ERROR_NO_UUID_FOR_ROOT,
				     "failed to get uuid for boot: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get installmirrorlist */
	state_local = zif_state_get_child (state);
	cache_dir = zif_config_get_string (priv->config,
					   "upgrade_cache_dir",
					   error);
	if (cache_dir == NULL) {
		ret = FALSE;
		goto out;
	}
	installmirrorlist_filename = g_build_filename (cache_dir, "installmirrorlist", NULL);
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

		/* gets package data */
		state_local = zif_state_get_child (state);
		ret = zif_release_get_package_data (release, data, state_local, error);
		if (!ret)
			goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* remove any previous upgrade kernels */
	ret = zif_release_remove_kernel (release, data, error);
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
	ret = zif_release_write_kickstart (release, data, error);
	if (!ret)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	ret = TRUE;
out:
	zif_download_location_clear (priv->download);
	g_free (boot_dir);
	g_free (cache_dir);
	g_free (installmirrorlist_filename);
	g_free (mtab_entry);
	if (data != NULL) {
		if (data->upgrade != NULL)
			g_object_unref (data->upgrade);
		if (data->key_file_treeinfo != NULL)
			g_key_file_free (data->key_file_treeinfo);
		g_free (data->images_section);
		g_free (data->uuid_root);
		g_free (data->uuid_boot);
		g_free (data);
	}
	if (md_mirrorlist != NULL)
		g_object_unref (md_mirrorlist);
	if (boot_file != NULL)
		g_object_unref (boot_file);
	return ret;
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
 * Return value: A new #ZifRelease instance.
 *
 * Since: 0.1.3
 **/
ZifRelease *
zif_release_new (void)
{
	ZifRelease *release;
	release = g_object_new (ZIF_TYPE_RELEASE, NULL);
	return ZIF_RELEASE (release);
}

