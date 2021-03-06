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

#include "config.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <sys/types.h>
#include <utime.h>

#include "zif-category.h"
#include "zif-changeset-private.h"
#include "zif-config.h"
#include "zif-delta.h"
#include "zif-depend.h"
#include "zif-depend-private.h"
#include "zif-download.h"
#include "zif-groups.h"
#include "zif.h"
#include "zif-history.h"
#include "zif-legal.h"
#include "zif-lock.h"
#include "zif-manifest.h"
#include "zif-md-comps.h"
#include "zif-md-delta.h"
#include "zif-md-filelists-sql.h"
#include "zif-md-filelists-xml.h"
#include "zif-md.h"
#include "zif-md-metalink.h"
#include "zif-md-mirrorlist.h"
#include "zif-md-other-sql.h"
#include "zif-md-primary-sql.h"
#include "zif-md-primary-xml.h"
#include "zif-md-updateinfo.h"
#include "zif-media.h"
#include "zif-monitor.h"
#include "zif-object-array.h"
#include "zif-package.h"
#include "zif-package-local.h"
#include "zif-package-meta.h"
#include "zif-package-private.h"
#include "zif-package-remote.h"
#include "zif-release.h"
#include "zif-repos.h"
#include "zif-state-private.h"
#include "zif-store-array.h"
#include "zif-store-directory.h"
#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-store-meta.h"
#include "zif-store-remote.h"
#include "zif-store-rhn.h"
#include "zif-string.h"
#include "zif-transaction.h"
#include "zif-update.h"
#include "zif-update-info.h"
#include "zif-utils-private.h"

static gboolean _has_network_access = TRUE;
static gchar *zif_tmpdir = NULL;

/** ver:1.0 ***********************************************************/
static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
_g_test_hang_check_cb (gpointer user_data)
{
	g_main_loop_quit (_test_loop);
	_test_loop_timeout_id = 0;
	return FALSE;
}

/**
 * _g_test_loop_run_with_timeout:
 **/
static void
_g_test_loop_run_with_timeout (guint timeout_ms)
{
	g_assert (_test_loop_timeout_id == 0);
	g_assert (_test_loop == NULL);
	_test_loop = g_main_loop_new (NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add (timeout_ms, _g_test_hang_check_cb, NULL);
	g_main_loop_run (_test_loop);
}

/**
 * zif_test_get_data_file:
 **/
static gchar *
zif_test_get_data_file (const gchar *filename)
{
	gboolean ret;
	gchar *full;
	char full_tmp[PATH_MAX];

	/* check to see if we are being run in the build root */
	full = g_build_filename ("..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret)
		goto out;
	g_free (full);

	/* check to see if we are being run in make check */
	full = g_build_filename ("..", "..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret)
		goto out;
	g_free (full);
	full = NULL;
out:
	/* canonicalize path */
	if (full != NULL && !g_str_has_prefix (full, "/")) {
		realpath (full, full_tmp);
		g_free (full);
		full = g_strdup (full_tmp);
	}
	return full;
}

static void
zif_store_directory_func (void)
{
	ZifStore *store;
	ZifState *state;
	gchar *path;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *packages;

	path = zif_test_get_data_file (".");
	store = zif_store_directory_new ();
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);
	ret = zif_store_directory_set_path (ZIF_STORE_DIRECTORY (store),
					    path,
					    TRUE,
					    &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (zif_store_get_size (store), ==, 0);

	/* get packages */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	packages = zif_store_get_packages (store, state, &error);
	g_assert_no_error (error);
	g_assert (packages != NULL);
	g_assert_cmpint (packages->len, ==, 3);

	g_ptr_array_unref (packages);
	g_object_unref (store);
	g_assert (store == NULL);
	g_object_unref (state);
	g_assert (state == NULL);
	g_free (path);
}

static void
zif_package_array_func (void)
{
	GPtrArray *array;
	ZifPackage *pkg;
	gboolean ret;
	GError *error = NULL;
	GTimer *timer;
	guint i;

	array = zif_package_array_new ();
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 0);

	/* add first i386 pkg */
	pkg = zif_package_new ();
	ret = zif_package_set_id (pkg, "hal;0.1-1.fc13;i386;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_ptr_array_add (array, pkg);

	/* add new i686 pkg */
	pkg = zif_package_new ();
	ret = zif_package_set_id (pkg, "hal;0.1-1.fc13;i686;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_ptr_array_add (array, pkg);

	/* filter by arch */
	zif_package_array_filter_best_arch (array, "i686");
	g_assert_cmpint (array->len, ==, 1);
	pkg = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (pkg), ==, "hal;0.1-1.fc13;i686;installed");

	/* add new x86_64 pkg */
	pkg = zif_package_new ();
	ret = zif_package_set_id (pkg, "hal;0.1-1.fc13;x86_64;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_ptr_array_add (array, pkg);

	/* filter by arch */
	zif_package_array_filter_best_arch (array, "x86_64");
	g_assert_cmpint (array->len, ==, 1);
	pkg = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (pkg), ==, "hal;0.1-1.fc13;x86_64;installed");

	/* add new x86_64 pkg */
	pkg = zif_package_new ();
	ret = zif_package_set_id (pkg, "dave;0.1-1.fc13;noarch;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_ptr_array_add (array, pkg);

	/* filter by arch */
	zif_package_array_filter_best_arch (array, "i686");
	g_assert_cmpint (array->len, ==, 1);
	pkg = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (pkg), ==, "dave;0.1-1.fc13;noarch;installed");

	g_ptr_array_unref (array);

	/* ensure we return x86_64 as newer than i386 */
	array = zif_package_array_new ();

	/* add first i386 pkg */
	pkg = zif_package_new ();
	ret = zif_package_set_id (pkg, "hal;0.1-1.fc13;i386;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_ptr_array_add (array, pkg);

	/* add new x86_64 pkg */
	pkg = zif_package_new ();
	ret = zif_package_set_id (pkg, "hal;0.1-1.fc13;x86_64;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_ptr_array_add (array, pkg);

	pkg = zif_package_array_get_newest (array, &error);
	g_assert_no_error (error);
	g_assert (pkg != NULL);
	g_assert_cmpstr (zif_package_get_id (pkg), ==, "hal;0.1-1.fc13;x86_64;installed");

	g_ptr_array_unref (array);

	/* check we filter newest */
	array = zif_package_array_new ();

	/* add old pkg */
	pkg = zif_package_new ();
	zif_package_set_installed (pkg, TRUE);
	ret = zif_package_set_id (pkg, "hal;0.1-1.fc13;i686;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_ptr_array_add (array, pkg);

	/* add newer pkg */
	pkg = zif_package_new ();
	zif_package_set_installed (pkg, TRUE);
	ret = zif_package_set_id (pkg, "hal;0.2-1.fc13;i686;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_ptr_array_add (array, pkg);

	/* add same pkg */
	pkg = zif_package_new ();
	zif_package_set_installed (pkg, FALSE);
	ret = zif_package_set_id (pkg, "hal;0.2-1.fc13;i686;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_ptr_array_add (array, pkg);

	/* filter by newest */
	zif_package_array_filter_newest (array);
	g_assert_cmpint (array->len, ==, 1);
	pkg = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (pkg), ==, "hal;0.2-1.fc13;i686;installed");

	g_ptr_array_unref (array);

	/* filter duplicates */
	array = zif_package_array_new ();

	/* add same pkg a few times */
	for (i = 0; i < 1000; i++) {
		pkg = zif_package_new ();
		ret = zif_package_set_id (pkg, "hal;0.2-1.fc13;i686;installed", NULL);
		g_assert (ret);
		g_ptr_array_add (array, pkg);
	}
	g_assert_cmpint (array->len, ==, 1000);

	timer = g_timer_new ();

	/* filter duplicates */
	zif_package_array_filter_duplicates (array);
	g_assert_cmpint (array->len, ==, 1);
	pkg = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (pkg), ==, "hal;0.2-1.fc13;i686;installed");
	g_debug ("took %.0lf ms to filter 1000 packages", 1000 * g_timer_elapsed (timer, NULL));
	g_timer_destroy (timer);

	g_ptr_array_unref (array);
}

static void
zif_release_func (void)
{
	gboolean ret;
	ZifRelease *release;
	GPtrArray *array;
	GError *error = NULL;
	ZifState *state;
	ZifUpgrade *upgrade;
	ZifDownload *download;
	gchar *filename;
	ZifConfig *config;
	gchar *filename_releases;
	gchar *filename_treeinfo;

	if (!_has_network_access)
		return;

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);

	filename = zif_test_get_data_file ("zif.conf");
	ret = zif_config_set_filename (config, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* dummy */
	zif_config_set_string (config, "upgrade_cache_dir", zif_tmpdir, NULL);
	zif_config_set_string (config, "upgrade_boot_dir", zif_tmpdir, NULL);
	zif_config_set_string (config, "upgrade_repo_dir", zif_tmpdir, NULL);
	zif_config_set_string (config, "basearch", "i386", NULL);
	zif_config_set_string (config, "upgrade_releases_uri",
			       "http://people.freedesktop.org/~hughsient/fedora/preupgrade/releases.txt", NULL);

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	download = zif_download_new ();
	g_object_add_weak_pointer (G_OBJECT (download), (gpointer *) &download);
	g_object_add_weak_pointer (G_OBJECT (download), (gpointer *) &download);
	release = zif_release_new ();
	g_object_add_weak_pointer (G_OBJECT (release), (gpointer *) &release);

	/* ensure file is not present */
	filename_releases = g_build_filename (zif_tmpdir, "releases.txt", NULL);
	g_unlink (filename_releases);
	filename_treeinfo = g_build_filename (zif_tmpdir, ".treeinfo", NULL);
	g_unlink (filename_treeinfo);

	/* get upgrades */
	array = zif_release_get_upgrades (release, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

	/* get detail */
	upgrade = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_upgrade_get_id (upgrade), ==, "Fedora 15 (Lovelock)");
	g_assert_cmpstr (zif_upgrade_get_baseurl (upgrade), ==, NULL);
	g_assert_cmpstr (zif_upgrade_get_mirrorlist (upgrade), ==,
			"http://people.freedesktop.org/~hughsient/fedora/preupgrade/mirrorlist"
			"?repo=fedora-15&arch=i386");
	g_assert_cmpstr (zif_upgrade_get_install_mirrorlist (upgrade), ==,
			 "http://people.freedesktop.org/~hughsient/fedora/preupgrade/installmirrorlist"
			 "?path=pub/fedora/linux/releases/15/Fedora/i386/os");
	g_assert_cmpint (zif_upgrade_get_version (upgrade), ==, 15);
	g_assert (zif_upgrade_get_enabled (upgrade));
	g_assert (zif_upgrade_get_stable (upgrade));

	g_ptr_array_unref (array);

	/* do a fake upgrade */
	zif_state_reset (state);
	ret = zif_release_upgrade_version (release, 15,
					   ZIF_RELEASE_UPGRADE_KIND_DEFAULT,
					   state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_free (filename_releases);
	g_free (filename_treeinfo);
	g_object_unref (release);
	g_assert (release == NULL);
	g_object_unref (download);
	g_assert (download == NULL);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static gint
zif_indirect_strcmp (gchar **a, gchar **b)
{
	return g_strcmp0 (*a, *b);
}

static void
zif_manifest_func (void)
{
	ZifManifest *manifest;
	gboolean ret;
	GError *error = NULL;
	GDir *dir;
	gchar *dirname;
	const gchar *filename;
	gchar *path;
	guint i;
	GPtrArray *array;
	ZifState *state;
	gchar *filename_tmp;
	ZifConfig *config;

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);

	filename_tmp = zif_test_get_data_file ("zif.conf");
	ret = zif_config_set_filename (config, filename_tmp, &error);
	g_free (filename_tmp);
	g_assert_no_error (error);
	g_assert (ret);

	/* create new manifest */
	manifest = zif_manifest_new ();
	g_object_add_weak_pointer (G_OBJECT (manifest), (gpointer *) &manifest);
	g_assert (manifest != NULL);

	/* open directory of test data */
	dirname = zif_test_get_data_file ("transactions");
	g_assert (dirname != NULL);
	dir = g_dir_open (dirname, 0, &error);
	g_assert_no_error (error);
	g_assert (dir != NULL);

	/* get manifests files */
	array = g_ptr_array_new_with_free_func (g_free);
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		if (g_str_has_suffix (filename, ".manifest")) {
			path = g_build_filename (dirname, filename, NULL);
			g_ptr_array_add (array, path);
		}
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);

	/* sort this */
	g_ptr_array_sort (array, (GCompareFunc) zif_indirect_strcmp);

	/* run each sample transactions */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	for (i = 0; i < array->len; i++) {
		filename = g_ptr_array_index (array, i);
		zif_state_reset (state);
		ret = zif_config_reset_default (config, &error);
		g_assert_no_error (error);
		g_assert (ret);
		ret = zif_manifest_check (manifest, filename, state, &error);
		g_assert_no_error (error);
		g_assert (ret);
	}

	g_ptr_array_unref (array);
	g_free (dirname);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (manifest);
	g_assert (manifest == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_transaction_func (void)
{
	ZifTransaction *transaction;
	ZifPackage *package;
	ZifPackage *package2;
	ZifStore *local;
	ZifState *state;
	GPtrArray *remotes;
	GPtrArray *packages;
	GError *error = NULL;
	gboolean ret;
	gchar *filename;
	ZifTransactionReason reason;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	transaction = zif_transaction_new ();
	zif_transaction_set_verbose (transaction, TRUE);

	/* create dummy package for testing */
	package = ZIF_PACKAGE (zif_package_local_new ());
	filename = zif_test_get_data_file ("depend-0.1-1.fc13.noarch.rpm");
	ret = zif_package_local_set_from_filename (ZIF_PACKAGE_LOCAL (package), filename, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);

	/* add to install list */
	ret = zif_transaction_add_install (transaction, package, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add again, shouldn't fail */
	package2 = zif_package_new ();
	ret = zif_package_set_id (package2, "depend;0.1-1.fc13;noarch;installed", &error);
	ret = zif_transaction_add_install (transaction, package2, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add to remove list */
	ret = zif_transaction_add_remove (transaction, package, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add again, shouldn't fail */
	ret = zif_transaction_add_remove (transaction, package2, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (package);
	g_object_unref (package2);
	g_object_unref (transaction);

	/* resolve */
	transaction = zif_transaction_new ();
	zif_transaction_set_verbose (transaction, TRUE);

	package = zif_package_meta_new ();
	ret = zif_package_set_id (package, "test;0.0.1;i386;data", &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = zif_transaction_add_install (transaction, package, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (package);

	local = zif_store_meta_new ();
	zif_transaction_set_store_local (transaction, local);
	remotes = zif_store_array_new ();
	zif_transaction_set_stores_remote (transaction, remotes);
	ret = zif_transaction_resolve (transaction, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_clear_error (&error);

	/* get results */
	packages = zif_transaction_get_remove (transaction);
	g_assert (packages != NULL);
	g_assert_cmpint (packages->len, ==, 0);
	g_ptr_array_unref (packages);

	/* check reason */
	packages = zif_transaction_get_install (transaction);
	g_assert (packages != NULL);
	g_assert_cmpint (packages->len, ==, 1);
	package = g_ptr_array_index (packages, 0);
	g_assert_cmpstr (zif_package_get_id (package), ==, "test;0.0.1;i386;data");
	reason = zif_transaction_get_reason (transaction, package, &error);
	g_assert_no_error (error);
	g_assert_cmpint (reason, ==, ZIF_TRANSACTION_REASON_INSTALL_USER_ACTION);
	g_ptr_array_unref (packages);

	/* prepare */
	zif_state_reset (state);
	ret = zif_transaction_prepare (transaction, state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_ptr_array_unref (remotes);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (local);
	g_object_unref (transaction);
}

static void
zif_changeset_func (void)
{
	gboolean ret;
	ZifChangeset *changeset;

	changeset = zif_changeset_new ();
	zif_changeset_set_description (changeset, "Update to latest stable version");
	g_assert (changeset != NULL);

	ret = zif_changeset_parse_header (changeset, "this-is-an-invalid-header", NULL);
	g_assert (!ret);

	/* success */
	ret = zif_changeset_parse_header (changeset, "Milan Crha <mcrha@redhat.com> - 2.29.91-1.fc13", NULL);
	g_assert (ret);
	g_assert_cmpstr (zif_changeset_get_description (changeset), ==, "Update to latest stable version");
	g_assert_cmpstr (zif_changeset_get_author (changeset), ==, "Milan Crha <mcrha@redhat.com>");
	g_object_unref (changeset);

	changeset = zif_changeset_new ();
	ret = zif_changeset_parse_header (changeset, "Milan Crha <mcrha at redhat.com> 2.29.91-1.fc13", NULL);
	g_assert (ret);
	g_assert_cmpstr (zif_changeset_get_author (changeset), ==, "Milan Crha <mcrha@redhat.com>");
	g_assert_cmpstr (zif_changeset_get_version (changeset), ==, "2.29.91-1.fc13");

	g_object_unref (changeset);
}

static void
zif_config_func (void)
{
	ZifConfig *config;
	gboolean ret;
	GError *error = NULL;
	gchar *value;
	gchar *basearch;
	guint len;
	gchar **array;
	gchar *filename;

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);

	filename = zif_test_get_data_file ("zif.conf");
	ret = zif_config_set_filename (config, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	value = zif_config_get_string (config, "cachedir", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (value, ==, "/var/cache/zif/$basearch/$releasever");
	g_free (value);

	value = zif_config_get_string (config, "notgoingtoexists", NULL);
	g_assert_cmpstr (value, ==, NULL);
	g_free (value);

	ret = zif_config_get_boolean (config, "exactarch", &error);
	g_assert_no_error (error);
	g_assert (!ret);

	ret = zif_config_set_string (config, "cachedir", "/etc/cache", &error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = zif_config_set_string (config, "cachedir", "/etc/cache", NULL);
	g_assert (ret);

	ret = zif_config_set_string (config, "cachedir", "/etc/dave", NULL);
	g_assert (!ret);

	value = zif_config_get_string (config, "cachedir", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (value, ==, "/etc/cache");
	g_free (value);

	ret = zif_config_reset_default (config, &error);
	g_assert (ret);

	value = zif_config_get_string (config, "cachedir", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (value, ==, "/var/cache/zif/$basearch/$releasever");
	g_free (value);

	value = zif_config_expand_substitutions (config, "http://fedora/4/6/moo.rpm", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (value, ==, "http://fedora/4/6/moo.rpm");
	g_free (value);

	array = zif_config_get_basearch_array (config);
	len = g_strv_length (array);
	g_assert_no_error (error);
	basearch = zif_config_get_string (config, "basearch", &error);
	if (g_strcmp0 (basearch, "i386") == 0) {
		g_assert_cmpint (len, ==, 5);
		g_assert_cmpstr (array[0], ==, "i386");
		value = zif_config_expand_substitutions (config, "http://fedora/$releasever/$basearch/moo.rpm", NULL);
		g_assert_cmpstr (value, ==, "http://fedora/13/i386/moo.rpm");
		g_free (value);
	} else {
		g_assert_cmpint (len, ==, 2);
		g_assert_cmpstr (array[0], ==, "x86_64");
		value = zif_config_expand_substitutions (config, "http://fedora/$releasever/$basearch/moo.rpm", NULL);
		g_assert_cmpstr (value, ==, "http://fedora/13/x86_64/moo.rpm");
		g_free (value);
	}
	g_free (basearch);

	g_object_unref (config);
	g_assert (config == NULL);
}

/**
 * zif_config_changed_func:
 *
 * Tests replacing the config file, for instance what happens when a
 * user upgrades zif using zif.
 * In this instance we want to preserve overridden state and use new
 * values from the config file
 **/
static void
zif_config_changed_func (void)
{
	gboolean ret;
	gchar *filename;
	gchar *value;
	GError *error = NULL;
	ZifConfig *config;

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);

	filename = g_build_filename (zif_tmpdir, "zif.conf", NULL);
	ret = g_file_set_contents (filename, "[main]\nconfig_schema_version=1\n", -1, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = zif_config_set_filename (config, filename, &error);
	g_assert_no_error (error);
	g_assert (ret);

	value = zif_config_get_string (config, "key", NULL);
	g_assert_cmpstr (value, ==, NULL);
	g_free (value);

	/* set override */
	ret = zif_config_set_string (config, "cachedir", "/etc/cache", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* touch file, and ensure file is autoloaded */
	ret = g_file_set_contents (filename, "[main]\nkey=value\nconfig_schema_version=1\n", -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* spin, and wait for GIO */
	_g_test_loop_run_with_timeout (2000);

	/* get a value from the new config */
	value = zif_config_get_string (config, "key", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (value, ==, "value");
	g_free (value);

	/* get override */
	value = zif_config_get_string (config, "cachedir", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (value, ==, "/etc/cache");
	g_free (value);

	g_object_unref (config);
	g_assert (config == NULL);
	g_free (filename);
}

static void
zif_db_func (void)
{
	gboolean ret;
	gchar *data;
	gchar *filename;
	GError *error = NULL;
	GPtrArray *array;
	ZifDb *db;
	ZifPackage *package;
	ZifString *string;

	db = zif_db_new ();

	/* set the root */
	ret = zif_db_set_root (db, zif_tmpdir, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* create a dummy package */
	package = zif_package_remote_new ();
	ret = zif_package_set_id (package, "PackageKit;0.1.2-14.fc13;i386;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* dummy set */
	string = zif_string_new ("8acc1b3457e3a5115ca2ad40cf0b3c121d2ab82d");
	zif_package_set_pkgid (package, string);
	zif_string_unref (string);

	/* write to the database */
	ret = zif_db_set_string (db, package, "from_repo", "fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check it exists */
	filename = g_build_filename (zif_tmpdir,
				     "P",
				     "8acc1b3457e3a5115ca2ad40cf0b3c121d2ab82d-"
				     "PackageKit-0.1.2-14.fc13-i386",
				     "from_repo",
				     NULL);
	ret = g_file_test (filename,
			   G_FILE_TEST_EXISTS);
	g_assert (ret);
	g_free (filename);

	/* read value */
	data = zif_db_get_string (db, package, "from_repo", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (data, ==, "fedora");
	g_free (data);

	/* get keys */
	array = zif_db_get_keys (db, package, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);
	g_object_unref (package);

	g_object_unref (db);
	db = zif_db_new ();

	/* set the root */
	filename = zif_test_get_data_file ("yumdb");
	ret = zif_db_set_root (db, filename, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);

	array = zif_db_get_packages (db, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 9);
	g_ptr_array_unref (array);

	g_object_unref (db);
}

static void
zif_depend_func (void)
{
	ZifDepend *depend;
	ZifDepend *need;
	gboolean ret;
	GError *error = NULL;
	const gchar *keys1[] = { "name",
				 "epoch",
				 "version",
				 "release",
				 "flags",
				 NULL };
	const gchar *vals1[] = { "kernel",
				 "1",
				 "2.6.0",
				 "1.fc15",
				 "GT",
				 NULL };
	const gchar *keys2[] = { "name",
				 NULL };
	const gchar *vals2[] = { "kernel",
				 NULL };

	depend = zif_depend_new ();
	zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_GREATER);
	zif_depend_set_name (depend, "kernel");
	zif_depend_set_version (depend, "2.6.0");

	g_assert_cmpstr (zif_depend_get_name (depend), ==, "kernel");
	g_assert_cmpstr (zif_depend_get_version (depend), ==, "2.6.0");
	g_assert_cmpint (zif_depend_get_flag (depend), ==, ZIF_DEPEND_FLAG_GREATER);
	g_assert_cmpstr (zif_depend_get_description (depend), ==, "[kernel > 2.6.0]");
	g_object_unref (depend);

	/* test parsing 1 form */
	depend = zif_depend_new ();
	ret = zif_depend_parse_description (depend, "kernel", &error);
	g_assert_cmpstr (zif_depend_get_name (depend), ==, "kernel");
	g_assert_cmpstr (zif_depend_get_version (depend), ==, NULL);
	g_assert_cmpint (zif_depend_get_flag (depend), ==, ZIF_DEPEND_FLAG_ANY);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (depend);

	/* test parsing 3 form */
	depend = zif_depend_new ();
	ret = zif_depend_parse_description (depend, "kernel >= 2.6.0", &error);
	g_assert_cmpstr (zif_depend_get_name (depend), ==, "kernel");
	g_assert_cmpstr (zif_depend_get_version (depend), ==, "2.6.0");
	g_assert_cmpint (zif_depend_get_flag (depend), ==, ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (depend);

	/* test parsing invalid */
	depend = zif_depend_new ();
	ret = zif_depend_parse_description (depend, "kernel 2.6.0", &error);
	g_assert_error (error, 1, 0);
	g_assert (!ret);
	g_clear_error (&error);
	g_object_unref (depend);

	/* test satisfiability */
	depend = zif_depend_new ();
	zif_depend_set_name (depend, "hal");
	zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (depend, "0.5.8-1.fc15");

	/* exact */
	need = zif_depend_new ();
	zif_depend_set_name (need, "hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (need, "0.5.8-1");
	ret = zif_depend_satisfies (depend, need);
	g_assert (ret);
	g_object_unref (need);

	/* exact with zero epoch */
	need = zif_depend_new ();
	zif_depend_set_name (need, "hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (need, "0:0.5.8-1");
	ret = zif_depend_satisfies (depend, need);
	g_assert (ret);
	g_object_unref (need);

	/* exact with no release */
	need = zif_depend_new ();
	zif_depend_set_name (need, "hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (need, "0.5.8");
	ret = zif_depend_satisfies (depend, need);
	g_assert (ret);
	g_object_unref (need);

	/* non version specific */
	need = zif_depend_new ();
	zif_depend_set_name (need, "hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_ANY);
	ret = zif_depend_satisfies (depend, need);
	g_assert (ret);
	g_object_unref (need);

	/* greater than */
	need = zif_depend_new ();
	zif_depend_set_name (need, "hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_GREATER);
	zif_depend_set_version (need, "0.5.7-1");
	ret = zif_depend_satisfies (depend, need);
	g_assert (ret);
	g_object_unref (need);

	/* greater or equal to */
	need = zif_depend_new ();
	zif_depend_set_name (need, "hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (need, "0.5.7-1");
	ret = zif_depend_satisfies (depend, need);
	g_assert (ret);
	g_object_unref (need);

	/* less than */
	need = zif_depend_new ();
	zif_depend_set_name (need, "hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_LESS);
	zif_depend_set_version (need, "0.5.9-1");
	ret = zif_depend_satisfies (depend, need);
	g_assert (ret);
	g_object_unref (need);

	/* less or equal to */
	need = zif_depend_new ();
	zif_depend_set_name (need, "hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_LESS | ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (need, "0.5.9-1");
	ret = zif_depend_satisfies (depend, need);
	g_assert (ret);
	g_object_unref (need);

	/* fail */
	need = zif_depend_new ();
	zif_depend_set_name (need, "hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (need, "0.5.9-1");
	ret = zif_depend_satisfies (depend, need);
	g_assert (!ret);
	g_object_unref (need);

	/* fail */
	need = zif_depend_new ();
	zif_depend_set_name (need, "not-hal");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (need, "0.5.8-1");
	ret = zif_depend_satisfies (depend, need);
	g_assert (!ret);
	g_object_unref (need);
	g_object_unref (depend);

	/* test satisfiability with no release */
	depend = zif_depend_new ();
	zif_depend_set_name (depend, "bash");
	zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (depend, "0.0.3");

	/* exact */
	need = zif_depend_new ();
	zif_depend_set_name (need, "bash");
	zif_depend_set_flag (need, ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (need, "0.0.3-1");
	ret = zif_depend_satisfies (depend, need);
	g_assert (ret);
	g_object_unref (need);

	g_object_unref (depend);

	/* create with data */
	depend = zif_depend_new_from_data (keys1, vals1);
	g_assert_cmpstr (zif_depend_get_name (depend), ==, "kernel");
	g_assert_cmpstr (zif_depend_get_version (depend), ==, "1:2.6.0-1.fc15");
	g_assert_cmpint (zif_depend_get_flag (depend), ==, ZIF_DEPEND_FLAG_GREATER);
	g_object_unref (depend);

	depend = zif_depend_new_from_data (keys2, vals2);
	g_assert_cmpstr (zif_depend_get_name (depend), ==, "kernel");
	g_assert_cmpstr (zif_depend_get_version (depend), ==, NULL);
	g_assert_cmpint (zif_depend_get_flag (depend), ==, ZIF_DEPEND_FLAG_ANY);
	g_object_unref (depend);
}

static guint _updates = 0;
static GMainLoop *_loop = NULL;

static void
zif_download_progress_changed (ZifDownload *download, guint value, gpointer data)
{
	_updates++;
}

static gboolean
zif_download_cancel_cb (GCancellable *cancellable)
{
	g_debug ("sending cancel");
	g_cancellable_cancel (cancellable);
	g_main_loop_quit (_loop);
	return FALSE;
}

static gpointer
zif_download_cancel_thread_cb (GCancellable *cancellable)
{
	g_debug ("thread running");
	g_timeout_add (50, (GSourceFunc) zif_download_cancel_cb, cancellable);
	_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (_loop);
	g_main_loop_unref (_loop);
	return NULL;
}

static void
zif_download_func (void)
{
	ZifDownload *download;
	ZifState *state;
	ZifConfig *config;
	GCancellable *cancellable = NULL;
	gboolean ret;
	gchar *filename;
	GError *error = NULL;

	download = zif_download_new ();
	g_object_add_weak_pointer (G_OBJECT (download), (gpointer *) &download);
	g_assert (download != NULL);
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	g_assert (state != NULL);
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);

	/* get config file */
	filename = zif_test_get_data_file ("zif.conf");
	ret = zif_config_set_filename (config, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* turn off slow mirror detection */
	zif_config_set_uint (config, "slow_server_speed", 0, NULL);

	/* add something sensible, but it won't resolve later on */
	ret = zif_download_location_add_uri (download, "http://www.bbc.co.uk/pub/", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add */
	ret = zif_download_location_add_uri (download, "http://people.freedesktop.org/~hughsient/fedora/preupgrade/", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add another */
	ret = zif_download_location_add_uri (download, "http://fubar.com/pub/", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* remove non existant */
	ret = zif_download_location_remove_uri (download, "http://fubar.com/davyjones/", &error);
	g_assert_error (error, ZIF_DOWNLOAD_ERROR, ZIF_DOWNLOAD_ERROR_FAILED);
	g_assert (!ret);
	g_clear_error (&error);

	/* remove fubar location */
	ret = zif_download_location_remove_uri (download, "http://fubar.com/pub/", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* download using the pool of uris (only the second will work) */
	zif_config_set_string (config, "failovermethod", "ordered", NULL);
	filename = g_build_filename (zif_tmpdir, "releases.txt", NULL);
	ret = zif_download_location_full (download,
					  "releases.txt",
					  filename,
					  397, /* size in bytes */
					  "text/plain,application/x-gzip", /* content type */
					  G_CHECKSUM_SHA256,
					  "c69baf7ef17843d9205e9553fbe037eff9502d91299068594c4c28e225827c6f",
					  state,
					  &error);
	/* special case running zif in a buildroot (no internet access) */
	if (!ret &&
	    error->domain == ZIF_DOWNLOAD_ERROR &&
	    error->code == ZIF_DOWNLOAD_ERROR_WRONG_STATUS) {
		g_assert (!ret);
		g_debug ("Failed to download, but in a buildroot, so ignoring");
		g_error_free (error);
		_has_network_access = FALSE;
		goto out;
	}
	g_assert_no_error (error);
	g_assert (ret);

	/* this failed to resolve, so this should have already been removed */
	ret = zif_download_location_remove_uri (download, "http://www.bbc.co.uk/pub/", &error);
	g_assert_error (error, ZIF_DOWNLOAD_ERROR, ZIF_DOWNLOAD_ERROR_FAILED);
	g_assert (!ret);
	g_clear_error (&error);

	/* this exists in no mirror */
	g_unlink (filename);
	ret = zif_download_location (download, "releases.bad", filename, state, &error);
	g_assert_error (error,
			ZIF_DOWNLOAD_ERROR,
			ZIF_DOWNLOAD_ERROR_WRONG_STATUS);
	g_assert (!ret);
	g_clear_error (&error);
	g_free (filename);

	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_download_progress_changed), NULL);
	cancellable = g_cancellable_new ();
	zif_state_set_cancellable (state, cancellable);

	filename = g_build_filename (zif_tmpdir, "Screenshot.png", NULL);
	zif_state_reset (state);
	ret = zif_download_file (download, "http://people.freedesktop.org/~hughsient/temp/Screenshot.png",
				 filename, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (_updates, >, 5);

	/* setup cancel */
	g_thread_new ("zif-self-test",
		      (GThreadFunc) zif_download_cancel_thread_cb,
		      cancellable);

	zif_state_reset (state);
	ret = zif_download_file (download, "http://people.freedesktop.org/~hughsient/temp/Screenshot.png",
				 filename, state, &error);
	g_assert_error (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_CANCELLED);
	g_assert (!ret);
	g_clear_error (&error);
	g_free (filename);

#if 0
	/* try to download a file from FTP */
	filename = g_build_filename (zif_tmpdir, "LATEST", NULL);
	ret = zif_download_file (download,
				 "ftp://ftp.gnome.org/pub/GNOME/sources/gnome-color-manager/2.29/LATEST-IS-2.29.4",
				 filename, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (g_file_test (filename, G_FILE_TEST_EXISTS));
	g_free (filename);
#endif
out:
	if (cancellable != NULL)
		g_object_unref (cancellable);
	g_object_unref (download);
	g_assert (download == NULL);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_groups_func (void)
{
	ZifGroups *groups;
	gboolean ret;
	GPtrArray *array;
	GError *error = NULL;
	const gchar *group;
	const gchar *category;
	gchar *filename;
	ZifState *state;

	groups = zif_groups_new ();
	g_assert (groups != NULL);

	filename = zif_test_get_data_file ("yum-comps-groups.conf");
	ret = zif_groups_set_mapping_file (groups, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	ret = zif_groups_load (groups, state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	zif_state_reset (state);
	array = zif_groups_get_groups (groups, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	group = g_ptr_array_index (array, 0);
	g_assert_cmpstr (group, ==, "admin-tools");

	zif_state_reset (state);
	array = zif_groups_get_categories (groups, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >, 100);
	g_ptr_array_unref (array);

	zif_state_reset (state);
	group = zif_groups_get_group_for_cat (groups, "language-support;kashubian-support", state, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (group, ==, "localization");

	zif_state_reset (state);
	array = zif_groups_get_cats_for_group (groups, "localization", state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >, 50);
	category = g_ptr_array_index (array, 0);
	g_assert_cmpstr (category, ==, "base-system;input-methods");
	g_ptr_array_unref (array);

	g_object_unref (groups);
	g_object_unref (state);
	g_assert (state == NULL);
}

static void
zif_legal_func (void)
{
	ZifLegal *legal;
	gboolean ret;
	gboolean is_free;
	GError *error = NULL;
	gchar *filename;

	legal = zif_legal_new ();
	g_assert (legal != NULL);

	/* set filename */
	filename = zif_test_get_data_file ("licenses.txt");
	zif_legal_set_filename (legal, filename);
	g_free (filename);

	ret = zif_legal_is_free (legal, "GPLv2+", &is_free, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (is_free);

	ret = zif_legal_is_free (legal, "Zend and wxWidgets", &is_free, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (is_free);

	ret = zif_legal_is_free (legal, "Zend and wxWidgets and MSCPL", &is_free, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (!is_free);

	g_object_unref (legal);
}

static guint _zif_lock_state_changed = 0;

static void
zif_lock_state_changed_cb (ZifLock *lock, guint bitfield, gpointer user_data)
{
	g_debug ("lock state now %i", bitfield);
	_zif_lock_state_changed++;
}

static void
zif_lock_func (void)
{
	ZifLock *lock;
	ZifConfig *config;
	gboolean ret;
	guint lock_id1;
	guint lock_id2;
	GError *error = NULL;
	gchar *pidfile;
	gchar *filename;

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);

	filename = zif_test_get_data_file ("zif.conf");
	ret = zif_config_set_filename (config, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	lock = zif_lock_new ();
	g_assert (lock != NULL);
	g_signal_connect (lock, "state-changed",
			  G_CALLBACK (zif_lock_state_changed_cb), NULL);

	/* set this to somewhere we can write to */
	pidfile = g_build_filename (zif_tmpdir, "zif.lock", NULL);
	zif_config_set_string (config, "pidfile", pidfile, NULL);

	/* nothing yet! */
	g_assert_cmpint (zif_lock_get_state (lock), ==, 0);
	ret = zif_lock_release (lock, 999, &error);
	g_assert_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_NOT_LOCKED);
	g_assert (!ret);
	g_clear_error (&error);

	/* take one */
	lock_id1 = zif_lock_take (lock,
				  ZIF_LOCK_TYPE_RPMDB,
				  ZIF_LOCK_MODE_PROCESS,
				  &error);
	g_assert_no_error (error);
	g_assert (lock_id1 != 0);
	g_assert_cmpint (zif_lock_get_state (lock), ==, 1 << ZIF_LOCK_TYPE_RPMDB);
	g_assert_cmpint (_zif_lock_state_changed, ==, 1);

	/* take a different one */
	lock_id2 = zif_lock_take (lock,
				  ZIF_LOCK_TYPE_REPO,
				  ZIF_LOCK_MODE_PROCESS,
				  &error);
	g_assert_no_error (error);
	g_assert (lock_id2 != 0);
	g_assert (lock_id2 != lock_id1);
	g_assert_cmpint (zif_lock_get_state (lock), ==, 1 << ZIF_LOCK_TYPE_RPMDB | 1 << ZIF_LOCK_TYPE_REPO);
	g_assert_cmpint (_zif_lock_state_changed, ==, 2);

	/* take two */
	lock_id1 = zif_lock_take (lock,
				  ZIF_LOCK_TYPE_RPMDB,
				  ZIF_LOCK_MODE_PROCESS,
				  &error);
	g_assert_no_error (error);
	g_assert (lock_id1 != 0);
	g_assert_cmpint (zif_lock_get_state (lock), ==, 1 << ZIF_LOCK_TYPE_RPMDB | 1 << ZIF_LOCK_TYPE_REPO);

	/* release one */
	ret = zif_lock_release (lock, lock_id1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* release different one */
	ret = zif_lock_release (lock, lock_id2, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* release two */
	ret = zif_lock_release (lock, lock_id1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* no more! */
	ret = zif_lock_release (lock, lock_id1, &error);
	g_assert_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_NOT_LOCKED);
	g_assert (!ret);
	g_clear_error (&error);
	g_assert_cmpint (zif_lock_get_state (lock), ==, 0);
	g_assert_cmpint (_zif_lock_state_changed, ==, 6);

	g_object_unref (lock);
	g_object_unref (config);
	g_assert (config == NULL);
	g_free (pidfile);
}

static gpointer
zif_self_test_lock_thread_one (gpointer data)
{
	GError *error = NULL;
	guint lock_id;
	ZifLock *lock = ZIF_LOCK (data);

	g_usleep (G_USEC_PER_SEC / 100);
	lock_id = zif_lock_take (lock,
				 ZIF_LOCK_TYPE_REPO,
				 ZIF_LOCK_MODE_PROCESS,
				 &error);
	g_assert_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_FAILED);
	g_assert_cmpint (lock_id, ==, 0);
	g_error_free (error);
	return NULL;
}

static void
zif_lock_threads_func (void)
{
	gboolean ret;
	gchar *filename;
	gchar *pidfile;
	GError *error = NULL;
	GThread *one;
	guint lock_id;
	ZifConfig *config;
	ZifLock *lock;

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);

	filename = zif_test_get_data_file ("zif.conf");
	ret = zif_config_set_filename (config, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* set this to somewhere we can write to */
	pidfile = g_build_filename (zif_tmpdir, "zif.lock", NULL);
	zif_config_set_string (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	/* take in master thread */
	lock = zif_lock_new ();
	lock_id = zif_lock_take (lock,
				 ZIF_LOCK_TYPE_REPO,
				 ZIF_LOCK_MODE_PROCESS,
				 &error);
	g_assert_no_error (error);
	g_assert_cmpint (lock_id, >, 0);

	/* attempt to take in slave thread (should fail) */
	one = g_thread_new ("zif-lock-one",
			    zif_self_test_lock_thread_one,
			    lock);

	/* block, waiting for thread */
	g_usleep (G_USEC_PER_SEC);

	/* release lock */
	ret = zif_lock_release (lock, lock_id, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_thread_unref (one);
	g_object_unref (lock);
	g_object_unref (config);
	g_assert (config == NULL);
	g_assert (config == NULL);
}

static void
zif_md_func (void)
{
	ZifMd *md;
	gboolean ret;
	GError *error = NULL;
	ZifState *state;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	md = zif_md_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	/* you can't load a baseclass */
	zif_md_set_id (md, "old-name-no-error");
	zif_md_set_id (md, "fedora");
	ret = zif_md_load (md, state, &error);
	g_assert_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_NO_SUPPORT);
	g_assert (!ret);
	g_assert (!zif_md_get_is_loaded (md));

	g_object_unref (md);
	g_object_unref (state);
	g_assert (state == NULL);
}

static void
zif_md_comps_func (void)
{
	ZifMd *md;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *id;
	ZifState *state;
	ZifCategory *category;
	gchar *filename;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	md = zif_md_comps_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	filename = zif_test_get_data_file ("fedora/comps-fedora.xml.gz");
	zif_md_set_id (md, "fedora");
	zif_md_set_filename (md, filename);
	zif_md_set_checksum_type (md, G_CHECKSUM_SHA256);
	zif_md_set_checksum (md, "02493204cfd99c1cab1c812344dfebbeeadbe0ae04ace5ad338e1d045dd564f1");
	zif_md_set_checksum_uncompressed (md, "1523fcdb34bb65f9f0964176d00b8ea6590febddb54521bf289f0d22e86d5fca");
	g_free (filename);

	array = zif_md_comps_get_categories (ZIF_MD_COMPS (md), state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_assert (zif_md_get_is_loaded (md));

	category = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_category_get_id (category), ==, "apps");
	g_assert_cmpstr (zif_category_get_name (category), ==, "Applications");
	g_assert_cmpstr (zif_category_get_summary (category), ==, "Applications to perform a variety of tasks");
	g_ptr_array_unref (array);

	zif_state_reset (state);
	array = zif_md_comps_get_groups_for_category (ZIF_MD_COMPS (md), "apps", state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);

	category = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_category_get_id (category), ==, "admin-tools");
	g_ptr_array_unref (array);

	zif_state_reset (state);
	array = zif_md_comps_get_packages_for_group (ZIF_MD_COMPS (md), "admin-tools", state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);

	/* and with full category id */
	zif_state_reset (state);
	array = zif_md_comps_get_packages_for_group (ZIF_MD_COMPS (md), "apps;admin-tools", state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);

	id = g_ptr_array_index (array, 0);
	g_assert_cmpstr (id, ==, "test");
	g_ptr_array_unref (array);

	g_object_unref (md);
	g_object_unref (state);
	g_assert (state == NULL);
}

static void
zif_md_filelists_sql_func (void)
{
	ZifMd *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *pkgid;
	ZifState *state;
	const gchar *data[] = { "/usr/bin/gnome-power-manager", NULL };
	gchar *filename;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	md = zif_md_filelists_sql_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	zif_md_set_id (md, "fedora");
	zif_md_set_checksum_type (md, G_CHECKSUM_SHA256);
	zif_md_set_checksum (md, "5a4b8374034cbf3e6ac654c19a613d74318da890bf22ebef3d2db90616dc5377");
	zif_md_set_checksum_uncompressed (md, "498cd5a1abe685bb0bae6dab92b518649f62decfe227c28e810981f1126a2a5a");
	filename = zif_test_get_data_file ("fedora/filelists.sqlite.bz2");
	zif_md_set_filename (md, filename);
	g_free (filename);
	ret = zif_md_load (md, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (md));

	zif_state_reset (state);
	array = zif_md_search_file (md, (gchar**)data, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

	pkgid = g_ptr_array_index (array, 0);
	g_assert_cmpstr (pkgid, ==, "888f5500947e6dafb215aaf4ca0cb789a12dab404397f2a37b3623a25ed72794");
	g_assert_cmpint (strlen (pkgid), ==, 64);
	g_ptr_array_unref (array);

	g_object_unref (md);
	g_object_unref (state);
	g_assert (state == NULL);
}

static void
zif_md_filelists_xml_func (void)
{
	ZifMd *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifState *state;
	gchar *pkgid;
	const gchar *data[] = { "/usr/lib/debug/usr/bin/gpk-prefs.debug", NULL };
	gchar *filename;
	ZifConfig *config;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	md = zif_md_filelists_xml_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	zif_md_set_id (md, "fedora");
	zif_md_set_checksum_type (md, G_CHECKSUM_SHA256);
	zif_md_set_checksum (md, "cadb324b10d395058ed22c9d984038927a3ea4ff9e0e798116be44b0233eaa49");
	zif_md_set_checksum_uncompressed (md, "8018e177379ada1d380b4ebf800e7caa95ff8cf90fdd6899528266719bbfdeab");
	filename = zif_test_get_data_file ("fedora/filelists.xml.gz");
	zif_md_set_filename (md, filename);
	g_free (filename);
	ret = zif_md_load (md, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (md));

	zif_state_reset (state);
	array = zif_md_search_file (md, (gchar**)data, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

	pkgid = g_ptr_array_index (array, 0);
	g_assert_cmpstr (pkgid, ==, "cec62d49c26d27b8584112d7d046782c578a097b81fe628d269d8afd7f1d54f4");
	g_ptr_array_unref (array);

	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (md);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_md_metalink_func (void)
{
	ZifMd *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *uri;
	ZifState *state;
	ZifConfig *config;
	gchar *filename;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	md = zif_md_metalink_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	zif_md_set_id (md, "fedora");
	filename = zif_test_get_data_file ("metalink.xml");
	zif_md_set_filename (md, filename);
	g_free (filename);
	ret = zif_md_load (md, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (md));

	zif_state_reset (state);
	array = zif_md_metalink_get_uris (ZIF_MD_METALINK (md), 50, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 43);

	uri = g_ptr_array_index (array, 0);
	g_assert_cmpstr (uri, ==, "http://www.mirrorservice.org/sites/download.fedora.redhat.com/pub/fedora/linux/development/13/i386/os/");
	g_ptr_array_unref (array);

	g_object_unref (md);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_md_mirrorlist_func (void)
{
	ZifMd *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *uri;
	ZifState *state;
	ZifConfig *config;
	gchar *filename;
	gchar *basearch;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	md = zif_md_mirrorlist_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	zif_md_set_id (md, "fedora");
	filename = zif_test_get_data_file ("mirrorlist.txt");
	zif_md_set_filename (md, filename);
	g_free (filename);
	ret = zif_md_load (md, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (md));

	zif_state_reset (state);
	array = zif_md_mirrorlist_get_uris (ZIF_MD_MIRRORLIST (md), state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 3);

	uri = g_ptr_array_index (array, 0);
	basearch = zif_config_get_string (config, "basearch", NULL);
	if (g_strcmp0 (basearch, "i386") == 0)
		g_assert_cmpstr (uri, ==, "http://rpm.livna.org/repo/13/i386/");
	else
		g_assert_cmpstr (uri, ==, "http://rpm.livna.org/repo/13/x86_64/");
	g_ptr_array_unref (array);
	g_free (basearch);

	g_object_unref (md);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_md_other_sql_func (void)
{
	ZifMd *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifChangeset *changeset;
	ZifState *state;
	ZifConfig *config;
	gchar *filename;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	zif_config_set_uint (config, "metadata_expire", 0, NULL);
	zif_config_set_uint (config, "mirrorlist_expire", 0, NULL);

	md = zif_md_other_sql_new ();
	g_object_add_weak_pointer (G_OBJECT (md), (gpointer *) &md);
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	zif_md_set_id (md, "fedora");
	filename = zif_test_get_data_file ("fedora/other.sqlite.bz2");
	zif_md_set_filename (md, filename);
	g_free (filename);
	zif_md_set_checksum_type (md, G_CHECKSUM_SHA256);
	zif_md_set_checksum (md, "b3ea68a8eed49d16ffaf9eb486095e15641fb43dcd33ef2424fbeed27adc416b");
	zif_md_set_checksum_uncompressed (md, "08df4b69b8304e24f17cb17d22f2fa328511eacad91ce5b92c03d7acb94c41d7");
	ret = zif_md_load (md, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (md));

	zif_state_reset (state);
	array = zif_md_get_changelog (md,
				      "3f75d650e5fe874713627c16081fe8134d0f1bd57f1810c5ce426757a9d0bc88",
				      state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 10);

	/* get first entry */
	changeset = g_ptr_array_index (array, 1);
	g_assert_cmpstr (zif_changeset_get_version (changeset), ==, "2.10.0-1");
	g_assert_cmpstr (zif_changeset_get_author (changeset), ==, "Matthias Clasen <mclasen@redhat.com>");
	g_assert_cmpstr (zif_changeset_get_description (changeset), ==, "- Update 2.10.0");

	/* remove array */
	g_ptr_array_unref (array);

	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (md);
	g_assert (md == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_md_primary_sql_func (void)
{
	ZifMd *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifPackage *package;
	ZifState *state;
	ZifConfig *config;
	GTimer *timer;
	gchar **tmp;
	guint i;
	const gchar *data[] = { "gnome-power-manager.i686", "gnome-color-manager.i686", NULL };
	const gchar *data_glob[] = { "gnome-*", NULL };
	const gchar *data_noarch[] = { "perl-Log-Message-Simple.i686", NULL };
	gchar *filename;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	md = zif_md_primary_sql_new ();
	g_object_add_weak_pointer (G_OBJECT (md), (gpointer *) &md);
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	zif_md_set_id (md, "fedora");
	zif_md_set_checksum_type (md, G_CHECKSUM_SHA256);
	zif_md_set_checksum (md, "3b7612fe14a6fbc06e3484e738edd08ca30ac14c2d86ea72feef8a39cfee757a");
	zif_md_set_checksum_uncompressed (md, "4981bf8b555f84f392455b5e91f09954b9f9e187f43c33921bce9cd911917210");
	filename = zif_test_get_data_file ("fedora/primary.sqlite.bz2");
	zif_md_set_filename (md, filename);
	g_free (filename);
	ret = zif_md_load (md, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (md));

	/* resolving by name.arch */
	zif_state_reset (state);
	array = zif_md_resolve_full (md,
				     (gchar**)data,
				     ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH,
				     state,
				     &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	package = g_ptr_array_index (array, 0);
	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_summary (package, state, NULL), ==,
			 "GNOME power management service");
	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_source_filename (package, state, NULL), ==,
			 "gnome-power-manager-2.30.1-1.fc13.src.rpm");
	g_ptr_array_unref (array);

	/* resolve a lot of items */
	timer = g_timer_new ();
	tmp = g_new0 (char *, 10000 + 1);
	for (i = 0; i < 10000; i++)
		tmp[i] = g_strdup_printf ("test%03i", i);
	zif_state_reset (state);
	array = zif_md_resolve_full (md,
				     tmp,
				     ZIF_STORE_RESOLVE_FLAG_USE_NAME,
				     state,
				     &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);
	g_assert_cmpint (g_timer_elapsed (timer, NULL), <, 1.0);
	g_timer_destroy (timer);
	g_strfreev (tmp);

	/* resolving by name and globbing */
	zif_state_reset (state);
	array = zif_md_resolve_full (md,
				     (gchar**)data_glob,
				     ZIF_STORE_RESOLVE_FLAG_USE_NAME |
				     ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
				     state,
				     &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);

	/* resolving by name.arch for a noarch package */
	zif_state_reset (state);
	array = zif_md_resolve_full (md,
				     (gchar**)data_noarch,
				     ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH,
				     state,
				     &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);

	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (md);
	g_assert (md == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_md_primary_xml_func (void)
{
	const gchar *data[] = { "gnome-power-manager.i686", NULL };
	const gchar *data_glob[] = { "gnome-power*", NULL };
	const gchar *data_noarch[] = { "PackageKit-docs.i686", NULL };
	gboolean ret;
	gchar *filename;
	GError *error = NULL;
	GPtrArray *array;
	GPtrArray *depends;
	ZifConfig *config;
	ZifDepend *depend;
	ZifMd *md;
	ZifPackage *package;
	ZifState *state;
	ZifStoreRemote *store_remote;

	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	zif_config_set_boolean (config, "network", FALSE, NULL);
	zif_config_set_uint (config, "metadata_expire", 0, NULL);
	zif_config_set_uint (config, "mirrorlist_expire", 0, NULL);
	g_free (filename);

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	/* get remote store */
	store_remote = ZIF_STORE_REMOTE (zif_store_remote_new ());
	filename = zif_test_get_data_file ("repos/fedora.repo");
	ret = zif_store_remote_set_from_file (store_remote, filename, "fedora", state, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	md = zif_md_primary_xml_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	zif_md_set_store (md, ZIF_STORE (store_remote));
	zif_md_set_id (md, "fedora");
	zif_md_set_checksum_type (md, G_CHECKSUM_SHA256);
	zif_md_set_checksum (md, "33a0eed8e12f445618756b18aa49d05ee30069d280d37b03a7a15d1ec954f833");
	zif_md_set_checksum_uncompressed (md, "52e4c37b13b4b23ae96432962186e726550b19e93cf3cbf7bf55c2a673a20086");
	filename = zif_test_get_data_file ("fedora/primary.xml.gz");
	zif_md_set_filename (md, filename);
	g_free (filename);
	zif_state_reset (state);
	ret = zif_md_load (md, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (md));

	/* resolving by name and globbing */
	zif_state_reset (state);
	array = zif_md_resolve_full (md,
				     (gchar**)data_glob,
				     ZIF_STORE_RESOLVE_FLAG_USE_NAME |
				     ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
				     state,
				     &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 3);
	g_ptr_array_unref (array);

	/* resolving by name.arch for a noarch package */
	zif_state_reset (state);
	array = zif_md_resolve_full (md,
				     (gchar**)data_noarch,
				     ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH,
				     state,
				     &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);

	/* resolving by name.arch */
	zif_state_reset (state);
	array = zif_md_resolve_full (md,
				     (gchar**)data,
				     ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH,
				     state,
				     &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

	/* check the provides array */
	package = g_ptr_array_index (array, 0);
	zif_state_reset (state);
	depends = zif_package_get_provides (package, state, &error);
	g_assert_no_error (error);
	g_assert (depends != NULL);
	g_assert_cmpint (depends->len, ==, 2);
	depend = g_ptr_array_index (depends, 0);
	g_assert_cmpstr (zif_depend_get_description (depend), ==,
			 "[gnome-power-manager = 2.31.1-1.258.20100330git.fc13]");
	g_ptr_array_unref (depends);

	/* check the requires array */
	package = g_ptr_array_index (array, 0);
	zif_state_reset (state);
	depends = zif_package_get_requires (package, state, &error);
	g_assert_no_error (error);
	g_assert (depends != NULL);
	g_assert_cmpint (depends->len, ==, 66);
	depend = g_ptr_array_index (depends, 0);
	g_assert_cmpstr (zif_depend_get_description (depend), ==,
			 "[libbonobo-activation.so.4 ~ ]");
	g_ptr_array_unref (depends);

	/* get provides */
	zif_state_reset (state);
	depends = zif_package_get_provides (package, state, &error);
	g_assert_no_error (error);
	g_assert (depends != NULL);
	g_assert_cmpint (depends->len, ==, 2);
	depend = g_ptr_array_index (depends, 0);
	g_assert_cmpstr (zif_depend_get_description (depend), ==,
			 "[gnome-power-manager = 2.31.1-1.258.20100330git.fc13]");
	g_ptr_array_unref (depends);

	/* check the source rpm filename */
	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_source_filename (package, state, NULL), ==,
			 "gnome-power-manager-2.31.1-1.258.20100330git.fc13.src.rpm");

	g_ptr_array_unref (array);

	/* what provides a non-existant depend */
	depends = zif_object_array_new ();
	depend = zif_depend_new ();
	ret = zif_depend_parse_description (depend,
					    "nothing",
					    &error);
	g_assert_no_error (error);
	g_assert (ret);
	zif_object_array_add (depends, depend);
	zif_state_reset (state);
	array = zif_md_what_provides (md,
				      depends,
				      state,
				      &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 0);
	g_object_unref (depend);
	g_ptr_array_unref (depends);
	g_ptr_array_unref (array);

	/* what provides g-p-m */
	depends = zif_object_array_new ();
	depend = zif_depend_new ();
	ret = zif_depend_parse_description (depend,
					    "gnome-power-manager",
					    &error);
	g_assert_no_error (error);
	g_assert (ret);
	zif_object_array_add (depends, depend);
	zif_state_reset (state);
	array = zif_md_what_provides (md,
				      depends,
				      state,
				      &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	package = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (package), ==,
			 "gnome-power-manager;2.31.1-1.258.20100330git.fc13;i686;fedora");
	g_object_unref (depend);
	g_ptr_array_unref (depends);
	g_ptr_array_unref (array);

	g_object_unref (store_remote);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (md);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_md_updateinfo_func (void)
{
	ZifMd *md;
	GError *error = NULL;
	GPtrArray *array;
	ZifState *state;
	ZifUpdate *update;
	gchar *filename;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	md = zif_md_updateinfo_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	zif_md_set_id (md, "fedora");
	filename = zif_test_get_data_file ("fedora/updateinfo.xml.gz");
	zif_md_set_filename (md, filename);
	g_free (filename);
	zif_md_set_checksum_type (md, G_CHECKSUM_SHA256);
	zif_md_set_checksum (md, "8dce3986a1841860db16b8b5a3cb603110825252b80a6eb436e5f647e5346955");
	zif_md_set_checksum_uncompressed (md, "2ad5aa9d99f475c4950f222696ebf492e6d15844660987e7877a66352098a723");
	array = zif_md_updateinfo_get_detail_for_package (ZIF_MD_UPDATEINFO (md), "device-mapper-libs;1.02.27-7.fc10;ppc64;fedora", state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert (zif_md_get_is_loaded (md));
	g_assert_cmpint (array->len, ==, 1);

	update = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_update_get_id (update), ==, "FEDORA-2008-9969");
	g_assert_cmpstr (zif_update_get_title (update), ==, "lvm2-2.02.39-7.fc10");
	g_assert_cmpstr (zif_update_get_description (update), ==, "Fix an incorrect path that prevents the clvmd init script from working and include licence files with the sub-packages.");

	g_ptr_array_unref (array);
	g_object_unref (md);
	g_object_unref (state);
	g_assert (state == NULL);
}

static void
zif_md_delta_func (void)
{
	ZifMd *md;
	GError *error = NULL;
	ZifState *state;
	ZifDelta *delta;
	gchar *filename;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	md = zif_md_delta_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (md));

	zif_md_set_id (md, "fedora");
	filename = zif_test_get_data_file ("fedora/prestodelta.xml.gz");
	zif_md_set_filename (md, filename);
	g_free (filename);
	zif_md_set_checksum_type (md, G_CHECKSUM_SHA256);
	zif_md_set_checksum (md, "157db37dce190775ff083cb51043e55da6e4abcabfe00584d2a69cc8fd327cae");
	zif_md_set_checksum_uncompressed (md, "64b7472f40d355efde22c2156bdebb9c5babe8f35a9f26c6c1ca6b510031d485");

	delta = zif_md_delta_search_for_package (ZIF_MD_DELTA (md),
						 "test;0.1-3.fc13;noarch;fedora",
						 "test;0.1-1.fc13;noarch;fedora",
						 state, &error);
	g_assert_no_error (error);
	g_assert (delta != NULL);
	g_assert (zif_md_get_is_loaded (md));
	g_assert_cmpstr (zif_delta_get_filename (delta), ==, "drpms/test-0.1-1.fc13_0.1-3.fc13.i686.drpm");
	g_assert_cmpstr (zif_delta_get_sequence (delta), ==, "test-0.1-1.fc13-9942652a8896b437f4ad8ab930cd32080230");
	g_assert_cmpstr (zif_delta_get_checksum (delta), ==, "000a2b879f9e52e96a6b3c7279b32afbf163cd90ec3887d03aef8aa115f45000");
	g_assert_cmpint (zif_delta_get_size (delta), ==, 81396);

	g_object_unref (delta);
	g_object_unref (md);
	g_object_unref (state);
	g_assert (state == NULL);
}

static void
zif_monitor_test_file_monitor_cb (ZifMonitor *monitor, GMainLoop *loop)
{
	g_main_loop_quit (loop);
}

static gboolean
zif_monitor_test_touch (gpointer data)
{
	gchar *filename;
	filename = zif_test_get_data_file ("repos/fedora.repo");
	utime (filename, NULL);
	g_free (filename);
	return FALSE;
}

static void
zif_monitor_func (void)
{
	ZifMonitor *monitor;
	gboolean ret;
	GError *error = NULL;
	GMainLoop *loop;
	gchar *filename;

	monitor = zif_monitor_new ();
	g_assert (monitor != NULL);

	loop = g_main_loop_new (NULL, TRUE);
	g_signal_connect (monitor, "changed", G_CALLBACK (zif_monitor_test_file_monitor_cb), loop);

	filename = zif_test_get_data_file ("repos/fedora.repo");
	ret = zif_monitor_add_watch (monitor, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* touch in 10ms */
	g_timeout_add (10, (GSourceFunc) zif_monitor_test_touch, loop);

	/* wait for changed */
	g_main_loop_unref (loop);
	g_object_unref (monitor);
}

static void
zif_package_func (void)
{
	ZifPackage *a;
	ZifPackage *b;
	gboolean ret;
	gint retval;
	GError *error = NULL;

	/* check compare */
	a = zif_package_new ();
	ret = zif_package_set_id (a, "colord;0.0.1-1.fc15;i386;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);
	b = zif_package_new ();
	ret = zif_package_set_id (b, "colord;0.0.2-1.fc14;i386;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);
	retval = zif_package_compare (a, b);
	g_assert_cmpint (retval, ==, -1);

	/* check compare with flags */
	a = zif_package_new ();
	zif_package_set_installed (a, TRUE);
	ret = zif_package_set_id (a, "colord;0.0.1-1.fc15;i386;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	b = zif_package_new ();
	zif_package_set_installed (b, FALSE);
	ret = zif_package_set_id (b, "colord;0.0.1-1.fc14;i386;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);
	retval = zif_package_compare_full (a, b, ZIF_PACKAGE_COMPARE_FLAG_CHECK_INSTALLED);
	g_assert_cmpint (retval, ==, 1);

	/* check compare with distro-sync */
	zif_package_set_compare_mode (a, ZIF_PACKAGE_COMPARE_MODE_DISTRO);
	retval = zif_package_compare (a, b);
	g_assert_cmpint (retval, ==, 1);

	g_object_unref (a);
	g_object_unref (b);

	/* check full version */
	a = zif_package_new ();
	ret = zif_package_set_id (a, "colord;0.0.1-1.fc15;i386;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);
	b = zif_package_new ();
	ret = zif_package_set_id (b, "colord-freeworld;0.0.2-1.fc14;i586;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);
	retval = zif_package_compare_full (a, b,
					   ZIF_PACKAGE_COMPARE_FLAG_CHECK_VERSION);
	g_assert_cmpint (retval, ==, -1);
	retval = zif_package_compare_full (a, b,
					   ZIF_PACKAGE_COMPARE_FLAG_CHECK_NAME);
	g_assert_cmpint (retval, <, 0);
	retval = zif_package_compare_full (a, b,
					   ZIF_PACKAGE_COMPARE_FLAG_CHECK_ARCH);
	g_assert_cmpint (retval, ==, 0);

	g_object_unref (a);
	g_object_unref (b);

	/* check setting the repo_id */
	a = zif_package_new ();
	zif_package_set_installed (a, TRUE);
	ret = zif_package_set_id (a, "colord;0.0.1-1.fc15;i386;installed", &error);
	g_assert_no_error (error);
	g_assert (ret);
	zif_package_set_repo_id (a, "fedora");
	g_assert_cmpstr (zif_package_get_id (a), ==, "colord;0.0.1-1.fc15;i386;installed:fedora");
	g_assert_cmpstr (zif_package_get_data (a), ==, "installed:fedora");
	g_object_unref (a);
}

static void
zif_package_local_func (void)
{
	ZifPackage *pkg;
	gboolean ret;
	GError *error = NULL;
	gchar *filename;
	const gchar *id;

	pkg = zif_package_local_new ();
	g_assert (pkg != NULL);

	filename = zif_test_get_data_file ("test-0.1-1.fc13.noarch.rpm");
	ret = zif_package_local_set_from_filename (ZIF_PACKAGE_LOCAL (pkg), filename, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (!zif_package_is_installed (pkg));
	g_free (filename);

	/* test getting the keys from an unsigned package */
	g_assert_cmpstr (zif_package_local_get_key_id (ZIF_PACKAGE_LOCAL (pkg)), ==, NULL);

	g_object_unref (pkg);

	/* test getting and adding the GPG public keys */
	pkg = zif_package_local_new ();
	filename = zif_test_get_data_file ("clamav-filesystem-0.96.3-1400.fc14.noarch.rpm");
	ret = zif_package_local_set_from_filename (ZIF_PACKAGE_LOCAL (pkg), filename, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);

	/* fedora key */
	id = zif_package_local_get_key_id (ZIF_PACKAGE_LOCAL (pkg));
	g_assert (g_str_has_prefix (id, "RSA/SHA256"));
	g_assert (g_str_has_suffix (id, "Key ID 421caddb97a1071f"));

	/* check trust */
	g_assert_cmpint (zif_package_get_trust_kind (pkg), ==,
			 ZIF_PACKAGE_TRUST_KIND_UNKNOWN);

	g_object_unref (pkg);
}

static void
zif_package_meta_func (void)
{
	ZifPackage *pkg;
	gboolean ret;
	GError *error = NULL;
	gchar *filename;
	ZifState *state;
	GPtrArray *depends;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	pkg = zif_package_meta_new ();
	g_assert (pkg != NULL);

	/* check trust */
	g_assert_cmpint (zif_package_get_trust_kind (pkg), ==,
			 ZIF_PACKAGE_TRUST_KIND_UNKNOWN);

	filename = zif_test_get_data_file ("test.spec");
	ret = zif_package_meta_set_from_filename (ZIF_PACKAGE_META(pkg), filename, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpstr (zif_package_get_id (pkg), ==, "test;0.1-1%{?dist};i386;meta");

	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_summary (pkg, state, NULL), ==, "Test package");

	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_license (pkg, state, NULL), ==, "GPLv2+");

	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_url (pkg, state, NULL), ==, "http://people.freedesktop.org/~hughsient/releases/");

	/* requires */
	zif_state_reset (state);
	depends = zif_package_get_requires (pkg, state, &error);
	g_assert_no_error (error);
	g_assert (depends != NULL);
	g_assert_cmpint (depends->len, ==, 0);
	g_clear_error (&error);

	/* conflicts */
	zif_state_reset (state);
	depends = zif_package_get_conflicts (pkg, state, &error);
	g_assert_no_error (error);
	g_assert (depends != NULL);
	g_assert_cmpint (depends->len, ==, 1);
	g_clear_error (&error);

	/* obsoletes */
	zif_state_reset (state);
	depends = zif_package_get_obsoletes (pkg, state, &error);
	g_assert_no_error (error);
	g_assert (depends != NULL);
	g_assert_cmpint (depends->len, ==, 1);
	g_clear_error (&error);

	/* provides */
	zif_state_reset (state);
	depends = zif_package_get_provides (pkg, state, &error);
	g_assert_no_error (error);
	g_assert (depends != NULL);
	g_assert_cmpint (depends->len, ==, 2); /* one explicit, one the package itself */
	g_clear_error (&error);

	g_free (filename);
	g_object_unref (pkg);
	g_object_unref (state);
	g_assert (state == NULL);
}

static void
zif_package_remote_func (void)
{
	gchar *filename;
	GError *error = NULL;
	ZifStoreRemote *store_remote;
	ZifPackage *package;
	ZifState *state;
	GPtrArray *changelog;
	ZifString *string;
	gboolean ret;
	ZifUpdate *update;
	gchar *pidfile;
	const gchar *cache_filename;
	ZifConfig *config;
	ZifRepos *repos;
	ZifStoreLocal *store;

	/* delete files we created */
	g_unlink ("../data/tests/./fedora/packages/powerman-2.3.5-2.fc13.i686.rpm");

	/* set this up as dummy */
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	zif_config_set_boolean (config, "network", TRUE, NULL);
	zif_config_set_boolean (config, "use_installed_history", FALSE, NULL);
	zif_config_set_uint (config, "metadata_expire", 0, NULL);
	zif_config_set_uint (config, "mirrorlist_expire", 0, NULL);
	g_free (filename);

	pidfile = g_build_filename (zif_tmpdir, "zif.lock", NULL);
	zif_config_set_string (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	filename = zif_test_get_data_file (".");
	zif_config_set_string (config, "cachedir", filename, NULL);
	g_free (filename);

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	store = ZIF_STORE_LOCAL (zif_store_local_new ());
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);
	filename = zif_test_get_data_file ("root");
	zif_store_local_set_prefix (store, filename, &error);
	g_free (filename);
	g_assert_no_error (error);

	repos = zif_repos_new ();
	filename = zif_test_get_data_file ("repos");
	ret = zif_repos_set_repos_dir (repos, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* get remote store */
	store_remote = ZIF_STORE_REMOTE (zif_store_remote_new ());
	zif_state_reset (state);
	filename = zif_test_get_data_file ("repos/fedora.repo");
	ret = zif_store_remote_set_from_file (store_remote, filename, "fedora", state, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* set a package ID that does exist */
	package = zif_package_remote_new ();
	g_object_add_weak_pointer (G_OBJECT (package), (gpointer *) &package);
	ret = zif_package_set_id (package, "gnome-power-manager;2.30.1-1.fc13;i686;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check trust */
	g_assert_cmpint (zif_package_get_trust_kind (package), ==,
			 ZIF_PACKAGE_TRUST_KIND_UNKNOWN);

	/* set remote store */
	zif_package_remote_set_store_remote (ZIF_PACKAGE_REMOTE (package), store_remote);

	/* check trust */
	g_assert_cmpint (zif_package_get_trust_kind (package), ==,
			 ZIF_PACKAGE_TRUST_KIND_PUBKEY_UNVERIFIED);

	/* get the update detail */
	zif_state_reset (state);
	update = zif_package_remote_get_update_detail (ZIF_PACKAGE_REMOTE (package), state, &error);
	g_assert_no_error (error);
	g_assert (update != NULL);
	g_assert_cmpstr (zif_update_get_id (update), ==, "FEDORA-2010-9999");

	changelog = zif_update_get_changelog (update);
	g_assert (changelog != NULL);
	g_assert_cmpint (changelog->len, ==, 1);

	g_object_unref (package);
	g_assert (package == NULL);

	/* set a package ID that does not exist */
	package = zif_package_remote_new ();
	ret = zif_package_set_id (package, "hal;2.30.1-1.fc13;i686;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* set remote store */
	zif_package_remote_set_store_remote (ZIF_PACKAGE_REMOTE (package), store_remote);

	/* get the update detail */
	zif_state_reset (state);
	update = zif_package_remote_get_update_detail (ZIF_PACKAGE_REMOTE (package), state, &error);
	g_assert_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED);
	g_assert (update == NULL);
	g_object_unref (package);
	g_clear_error (&error);

	/* set a package ID that does not exist */
	package = zif_package_remote_new ();
	ret = zif_package_set_id (package, "hal;2.30.1-1.fc13;i686;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* set location */
	string = zif_string_new ("Packages/powerman-2.3.5-2.fc13.i686.rpm");
	zif_package_set_location_href (package, string);
	zif_string_unref (string);

	/* set size */
	zif_package_set_size (package, 156896);

	/* set remote store */
	zif_package_remote_set_store_remote (ZIF_PACKAGE_REMOTE (package), store_remote);

	/* check not downloaded */
	zif_state_reset (state);
	cache_filename = zif_package_get_cache_filename (package, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (!g_file_test (cache_filename, G_FILE_TEST_EXISTS));

	if (!_has_network_access)
		goto out;

	/* download it */
	ret = zif_package_remote_download (ZIF_PACKAGE_REMOTE (package), NULL, state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check downloaded */
	zif_state_reset (state);
	cache_filename = zif_package_get_cache_filename (package, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (g_file_test (cache_filename, G_FILE_TEST_EXISTS));

	/* delete files we created */
	g_unlink ("../data/tests/./fedora/packages/powerman-2.3.5-2.fc13.i686.rpm");
out:
	g_object_unref (package);
	g_object_unref (repos);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (store);
	g_assert (store == NULL);
	g_object_unref (store_remote);
	g_ptr_array_unref (changelog);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_repos_func (void)
{
	ZifStoreRemote *store;
	ZifConfig *config;
	ZifRepos *repos;
	ZifState *state;
	GPtrArray *array;
	GError *error = NULL;
	gboolean ret;
	gchar *filename;
	gchar *pidfile;
	gchar **pubkey;

	/* set this up as dummy */
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	pidfile = g_build_filename (zif_tmpdir, "zif.lock", NULL);
	zif_config_set_string (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	repos = zif_repos_new ();
	g_object_add_weak_pointer (G_OBJECT (repos), (gpointer *) &repos);
	g_assert (repos != NULL);

	/* use state object */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	filename = zif_test_get_data_file ("repos");
	ret = zif_repos_set_repos_dir (repos, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	array = zif_repos_get_stores (repos, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 4);
	g_ptr_array_unref (array);

	zif_state_reset (state);
	array = zif_repos_get_stores_enabled (repos, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 4);

	/* disable one store and reget */
	store = g_ptr_array_index (array, 1);
	zif_store_set_enabled (ZIF_STORE (store), FALSE);
	g_ptr_array_unref (array);

	zif_state_reset (state);
	array = zif_repos_get_stores_enabled (repos, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 3);

	/* check returns error for invalid */
	zif_state_reset (state);
	store = zif_repos_get_store (repos, "does-not-exist", state, &error);
	g_assert (store == NULL);
	g_assert_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED);
	g_clear_error (&error);

	/* get ref for next test */
	store = g_object_ref (g_ptr_array_index (array, 0));
	g_ptr_array_unref (array);

	/* ensure we expanded everything */
	zif_state_reset (state);
	g_assert (g_strstr_len (zif_store_remote_get_name (store, state, NULL), -1, "$") == NULL);
	g_object_unref (store);

	/* ensure we got the pubkey */
	pubkey = zif_store_remote_get_pubkey (store);
	g_assert (zif_store_remote_get_pubkey_enabled (store));
	g_assert (pubkey != NULL);
	g_assert (g_str_has_prefix (pubkey[0], "file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-"));

	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (repos);
	g_assert (repos == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static guint _allow_cancel_updates = 0;
static guint _action_updates = 0;
static guint _package_progress_updates = 0;
static guint _last_percent = 0;
static guint _last_subpercent = 0;

static void
zif_state_test_percentage_changed_cb (ZifState *state, guint value, gpointer data)
{
	_last_percent = value;
	_updates++;
}

static void
zif_state_test_subpercentage_changed_cb (ZifState *state, guint value, gpointer data)
{
	_last_subpercent = value;
}

static void
zif_state_test_allow_cancel_changed_cb (ZifState *state, gboolean allow_cancel, gpointer data)
{
	_allow_cancel_updates++;
}

static void
zif_state_test_action_changed_cb (ZifState *state, ZifStateAction action, gpointer data)
{
	_action_updates++;
}

static void
zif_state_test_package_progress_changed_cb (ZifState *state,
					    const gchar *package_id,
					    ZifStateAction action,
					    guint percentage,
					    gpointer data)
{
	g_assert (data == NULL);
	g_debug ("%s now %s at %u",
		 package_id,
		 zif_state_action_to_string (action),
		 percentage);
	_package_progress_updates++;
}

static void
zif_state_func (void)
{
	gboolean ret;
	guint i;
	ZifState *state;

	for (i = 0; i < ZIF_STATE_ACTION_UNKNOWN ; i++)
		g_assert (zif_state_action_to_string (i) != NULL);

	_updates = 0;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	g_assert (state != NULL);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "subpercentage-changed", G_CALLBACK (zif_state_test_subpercentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (zif_state_test_allow_cancel_changed_cb), NULL);
	g_signal_connect (state, "action-changed", G_CALLBACK (zif_state_test_action_changed_cb), NULL);
	g_signal_connect (state, "package-progress-changed", G_CALLBACK (zif_state_test_package_progress_changed_cb), NULL);

	g_assert (zif_state_get_allow_cancel (state));
	g_assert_cmpint (zif_state_get_action (state), ==, ZIF_STATE_ACTION_UNKNOWN);

	zif_state_set_allow_cancel (state, TRUE);
	g_assert (zif_state_get_allow_cancel (state));

	zif_state_set_allow_cancel (state, FALSE);
	g_assert (!zif_state_get_allow_cancel (state));
	g_assert_cmpint (_allow_cancel_updates, ==, 1);

	/* stop never started */
	g_assert (!zif_state_action_stop (state));

	/* repeated */
	g_assert (zif_state_action_start (state, ZIF_STATE_ACTION_DOWNLOADING, NULL));
	g_assert (!zif_state_action_start (state, ZIF_STATE_ACTION_DOWNLOADING, NULL));
	g_assert_cmpint (zif_state_get_action (state), ==, ZIF_STATE_ACTION_DOWNLOADING);
	g_assert (zif_state_action_stop (state));
	g_assert_cmpint (zif_state_get_action (state), ==, ZIF_STATE_ACTION_UNKNOWN);
	g_assert_cmpint (_action_updates, ==, 2);
	g_assert_cmpstr (zif_state_action_to_string (ZIF_STATE_ACTION_DOWNLOADING), ==, "downloading");

	ret = zif_state_set_number_steps (state, 5);
	g_assert (ret);

	ret = zif_state_done (state, NULL);
	g_assert (ret);

	g_assert_cmpint (_updates, ==, 1);

	g_assert_cmpint (_last_percent, ==, 20);

	ret = zif_state_done (state, NULL);
	ret = zif_state_done (state, NULL);
	ret = zif_state_done (state, NULL);
	zif_state_set_package_progress (state,
					"hal;0.0.1;i386;fedora",
					ZIF_STATE_ACTION_DOWNLOADING,
					50);
	g_assert (zif_state_done (state, NULL));

	g_assert (!zif_state_done (state, NULL));
	g_assert_cmpint (_updates, ==, 5);
	g_assert_cmpint (_package_progress_updates, ==, 1);
	g_assert_cmpint (_last_percent, ==, 100);

	/* ensure allow cancel as we're done */
	g_assert (zif_state_get_allow_cancel (state));

	g_object_unref (state);
	g_assert (state == NULL);

	/* check we've not leaked anything */}

static void
zif_state_child_func (void)
{
	gboolean ret;
	ZifState *state;
	ZifState *child;

	/* reset */
	_updates = 0;
	_allow_cancel_updates = 0;
	_action_updates = 0;
	_package_progress_updates = 0;
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	zif_state_set_allow_cancel (state, TRUE);
	zif_state_set_number_steps (state, 2);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "subpercentage-changed", G_CALLBACK (zif_state_test_subpercentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (zif_state_test_allow_cancel_changed_cb), NULL);
	g_signal_connect (state, "action-changed", G_CALLBACK (zif_state_test_action_changed_cb), NULL);
	g_signal_connect (state, "package-progress-changed", G_CALLBACK (zif_state_test_package_progress_changed_cb), NULL);

	// state: |-----------------------|-----------------------|
	// step1: |-----------------------|
	// child:                         |-------------|---------|

	/* PARENT UPDATE */
	g_debug ("parent update #1");
	ret = zif_state_done (state, NULL);

	g_assert ((_updates == 1));
	g_assert ((_last_percent == 50));

	/* set parent state */
	g_debug ("setting: depsolving-conflicts");
	zif_state_action_start (state,
				ZIF_STATE_ACTION_DEPSOLVING_CONFLICTS,
				"hal;0.1.0-1;i386;fedora");

	/* now test with a child */
	child = zif_state_get_child (state);
	zif_state_set_number_steps (child, 2);

	/* check child inherits parents action */
	g_assert_cmpint (zif_state_get_action (child), ==,
			 ZIF_STATE_ACTION_DEPSOLVING_CONFLICTS);

	/* set child non-cancellable */
	zif_state_set_allow_cancel (child, FALSE);

	/* ensure both are disallow-cancel */
	g_assert (!zif_state_get_allow_cancel (child));
	g_assert (!zif_state_get_allow_cancel (state));

	/* CHILD UPDATE */
	g_debug ("setting: loading-rpmdb");
	g_assert (zif_state_action_start (child, ZIF_STATE_ACTION_LOADING_RPMDB, NULL));
	g_assert_cmpint (zif_state_get_action (child), ==,
			 ZIF_STATE_ACTION_LOADING_RPMDB);

	g_debug ("child update #1");
	ret = zif_state_done (child, NULL);
	zif_state_set_package_progress (child,
					"hal;0.0.1;i386;fedora",
					ZIF_STATE_ACTION_DOWNLOADING,
					50);

	g_assert_cmpint (_updates, ==, 2);
	g_assert_cmpint (_last_percent, ==, 75);
	g_assert_cmpint (_package_progress_updates, ==, 1);

	/* child action */
	g_debug ("setting: downloading");
	g_assert (zif_state_action_start (child,
					  ZIF_STATE_ACTION_DOWNLOADING,
					  NULL));
	g_assert_cmpint (zif_state_get_action (child), ==,
			 ZIF_STATE_ACTION_DOWNLOADING);

	/* CHILD UPDATE */
	g_debug ("child update #2");
	ret = zif_state_done (child, NULL);

	g_assert_cmpint (zif_state_get_action (state), ==,
			 ZIF_STATE_ACTION_DEPSOLVING_CONFLICTS);
	g_assert (zif_state_action_stop (state));
	g_assert (!zif_state_action_stop (state));
	g_assert_cmpint (zif_state_get_action (state), ==,
			 ZIF_STATE_ACTION_UNKNOWN);
	g_assert_cmpint (_action_updates, ==, 6);

	g_assert_cmpint (_updates, ==, 3);
	g_assert_cmpint (_last_percent, ==, 100);

	/* ensure the child finishing cleared the allow cancel on the parent */
	ret = zif_state_get_allow_cancel (state);
	g_assert (ret);

	/* PARENT UPDATE */
	g_debug ("parent update #2");
	ret = zif_state_done (state, NULL);
	g_assert (ret);

	/* ensure we ignored the duplicate */
	g_assert_cmpint (_updates, ==, 3);
	g_assert_cmpint (_last_percent, ==, 100);

	g_object_unref (state);
	g_assert (state == NULL);

	/* check we've not leaked anything */}

static void
zif_state_parent_one_step_proxy_func (void)
{
	ZifState *state;
	ZifState *child;

	/* reset */
	_updates = 0;
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	zif_state_set_number_steps (state, 1);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "subpercentage-changed", G_CALLBACK (zif_state_test_subpercentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (zif_state_test_allow_cancel_changed_cb), NULL);

	/* now test with a child */
	child = zif_state_get_child (state);
	zif_state_set_number_steps (child, 2);

	/* CHILD SET VALUE */
	zif_state_set_percentage (child, 33);

	/* ensure 1 updates for state with one step and ensure using child value as parent */
	g_assert (_updates == 1);
	g_assert (_last_percent == 33);

	g_object_unref (state);
	g_assert (state == NULL);

	/* check we've not leaked anything */}

static void
zif_state_non_equal_steps_func (void)
{
	gboolean ret;
	GError *error = NULL;
	ZifState *state;
	ZifState *child;
	ZifState *child_child;

	/* test non-equal steps */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	zif_state_set_enable_profile (state, TRUE);
	ret = zif_state_set_steps (state,
				   &error,
				   20, /* prepare */
				   60, /* download */
				   10, /* install */
				   -1);
	g_assert_error (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_INVALID);
	g_assert (!ret);
	g_clear_error (&error);

	/* okay this time */
	ret = zif_state_set_steps (state, &error, 20, 60, 20, -1);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify nothing */
	g_assert_cmpint (zif_state_get_percentage (state), ==, 0);

	/* child step should increment according to the custom steps */
	child = zif_state_get_child (state);
	zif_state_set_number_steps (child, 2);

	/* start child */
	g_usleep (9 * 10 * 1000);
	ret = zif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify 10% */
	g_assert_cmpint (zif_state_get_percentage (state), ==, 10);

	/* finish child */
	g_usleep (9 * 10 * 1000);
	ret = zif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = zif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify 20% */
	g_assert_cmpint (zif_state_get_percentage (state), ==, 20);

	/* child step should increment according to the custom steps */
	child = zif_state_get_child (state);
	ret = zif_state_set_steps (child,
				   &error,
				   25,
				   75,
				   -1);

	/* start child */
	g_usleep (25 * 10 * 1000);
	ret = zif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify bilinear interpolation is working */
	g_assert_cmpint (zif_state_get_percentage (state), ==, 35);

	/*
	 * 0        20                             80         100
	 * |---------||----------------------------||---------|
	 *            |       35                   |
	 *            |-------||-------------------| (25%)
	 *                     |              75.5 |
	 *                     |---------------||--| (90%)
	 */
	child_child = zif_state_get_child (child);
	ret = zif_state_set_steps (child_child,
				   &error,
				   90,
				   10,
				   -1);
	g_assert_no_error (error);
	g_assert (ret);

	ret = zif_state_done (child_child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify bilinear interpolation (twice) is working for subpercentage */
	g_assert_cmpint (zif_state_get_percentage (state), ==, 75);

	ret = zif_state_done (child_child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* finish child */
	g_usleep (25 * 10 * 1000);
	ret = zif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = zif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify 80% */
	g_assert_cmpint (zif_state_get_percentage (state), ==, 80);

	g_usleep (19 * 10 * 1000);

	ret = zif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify 100% */
	g_assert_cmpint (zif_state_get_percentage (state), ==, 100);

	g_object_unref (state);
	g_assert (state == NULL);

	/* check we've not leaked anything */}

static void
zif_state_no_progress_func (void)
{
	gboolean ret;
	GError *error = NULL;
	ZifState *state;
	ZifState *child;

	/* test a state where we don't care about progress */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	zif_state_set_report_progress (state, FALSE);

	zif_state_set_number_steps (state, 3);
	g_assert_cmpint (zif_state_get_percentage (state), ==, 0);

	ret = zif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (zif_state_get_percentage (state), ==, 0);

	ret = zif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	child = zif_state_get_child (state);
	g_assert (child != NULL);
	zif_state_set_number_steps (child, 2);
	ret = zif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = zif_state_done (child, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (zif_state_get_percentage (state), ==, 0);

	g_object_unref (state);
	g_assert (state == NULL);

	/* check we've not leaked anything */}

static void
zif_state_finish_func (void)
{
	gboolean ret;
	GError *error = NULL;
	ZifState *state;
	ZifState *child;

	/* check straight finish */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	zif_state_set_number_steps (state, 3);

	child = zif_state_get_child (state);
	zif_state_set_number_steps (child, 3);
	ret = zif_state_finished (child, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* parent step done after child finish */
	ret = zif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_object_unref (state);
	g_assert (state == NULL);

	/* check we've not leaked anything */}

static gboolean
zif_state_error_handler_cb (const GError *error, gpointer user_data)
{
	/* emit a warning, this isn't fatal */
	g_debug ("ignoring errors: %s", error->message);
	return TRUE;
}

static void
zif_state_error_handler_func (void)
{
	gboolean ret;
	GError *error = NULL;
	ZifState *state;
	ZifState *child;

	/* test error ignoring */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	error = g_error_new (1, 0, "this is error: %i", 999);
	ret = zif_state_error_handler (state, error);
	g_assert (!ret);

	/* ensure child also fails */
	child = zif_state_get_child (state);
	ret = zif_state_error_handler (child, error);
	g_assert (!ret);

	/* pass all errors */
	zif_state_set_error_handler (state, zif_state_error_handler_cb, NULL);
	ret = zif_state_error_handler (state, error);
	g_assert (ret);

	/* ensure existing child also gets error handler passed down to it */
	ret = zif_state_error_handler (child, error);
	g_assert (ret);

	g_object_unref (state);
	g_assert (state == NULL);
	g_clear_error (&error);

	/* check we've not leaked anything */
	/* test new child gets error handler passed to it */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	error = g_error_new (1, 0, "this is error: %i", 999);
	zif_state_set_error_handler (state, zif_state_error_handler_cb, NULL);
	child = zif_state_get_child (state);
	ret = zif_state_error_handler (child, error);
	g_assert (ret);

	g_object_unref (state);
	g_assert (state == NULL);
	g_clear_error (&error);

	/* check we've not leaked anything */}

static void
zif_state_speed_func (void)
{
	ZifState *state;

	/* speed averaging test */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	g_assert_cmpint (zif_state_get_speed (state), ==, 0);
	zif_state_set_speed (state, 100);
	g_assert_cmpint (zif_state_get_speed (state), ==, 100);
	zif_state_set_speed (state, 200);
	g_assert_cmpint (zif_state_get_speed (state), ==, 150);
	zif_state_set_speed (state, 300);
	g_assert_cmpint (zif_state_get_speed (state), ==, 200);
	zif_state_set_speed (state, 400);
	g_assert_cmpint (zif_state_get_speed (state), ==, 250);
	zif_state_set_speed (state, 500);
	g_assert_cmpint (zif_state_get_speed (state), ==, 300);
	zif_state_set_speed (state, 600);
	g_assert_cmpint (zif_state_get_speed (state), ==, 400);
	g_object_unref (state);
	g_assert (state == NULL);

	/* check we've not leaked anything */}

static gboolean
zif_state_take_lock_cb (ZifState *state,
			ZifLock *lock,
			ZifLockType lock_type,
			GError **error,
			gpointer user_data)
{
	/* just return success without asking or writing any files */
	return TRUE;
}

static void
zif_state_finished_func (void)
{
	ZifState *state_local;
	ZifState *state;
	gboolean ret;
	GError *error = NULL;
	guint i;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	ret = zif_state_set_steps (state,
				   &error,
				   90,
				   10,
				   -1);
	g_assert_no_error (error);
	g_assert (ret);

	zif_state_set_allow_cancel (state, FALSE);
	zif_state_action_start (state,
				ZIF_STATE_ACTION_LOADING_RPMDB, "/");

	state_local = zif_state_get_child (state);
	zif_state_set_report_progress (state_local, FALSE);

	for (i = 0; i < 10; i++) {
		/* check cancelled (okay to reuse as we called
		 * zif_state_set_report_progress before)*/
		ret = zif_state_done (state_local, &error);
		g_assert_no_error (error);
		g_assert (ret);
	}

	/* turn checks back on */
	zif_state_set_report_progress (state_local, TRUE);
	ret = zif_state_finished (state_local, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* this section done */
	ret = zif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* this section done */
	ret = zif_state_done (state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_object_unref (state);
	g_assert (state == NULL);
}

static void
zif_state_locking_func (void)
{
	gboolean ret;
	gchar *filename;
	gchar *pidfile;
	GError *error = NULL;
	ZifConfig *config;
	ZifState *state;

	/* locking test */
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	pidfile = g_build_filename (zif_tmpdir, "zif.lock", NULL);
	zif_config_set_string (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	zif_state_set_lock_handler (state, zif_state_take_lock_cb, NULL);

	/* lock once */
	ret = zif_state_take_lock (state,
				   ZIF_LOCK_TYPE_RPMDB,
				   ZIF_LOCK_MODE_PROCESS,
				   &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* succeeded, even again */
	ret = zif_state_take_lock (state,
				   ZIF_LOCK_TYPE_RPMDB,
				   ZIF_LOCK_MODE_PROCESS,
				   &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_store_local_func (void)
{
	const gchar *package_id;
	const gchar *to_array[] = { NULL, NULL, NULL };
	gboolean ret;
	GCancellable *cancellable;
	gchar *filename;
	gchar *pidfile;
	gchar **split;
	GError *error = NULL;
	GPtrArray *array;
	GPtrArray *depend_array;
	guint elapsed;
	ZifConfig *config;
	ZifDepend *depend;
	ZifGroups *groups;
	ZifLegal *legal;
	ZifPackage *package;
	ZifState *state;
	ZifStoreLocal *store;

	/* set these up as dummy */
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	pidfile = g_build_filename (zif_tmpdir, "zif.lock", NULL);
	zif_config_set_string (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	/* set this to something that can't exist */
	zif_config_set_string (config, "history_db", "/dev/mapper/foobar", NULL);

	filename = zif_test_get_data_file ("licenses.txt");
	legal = zif_legal_new ();
	zif_legal_set_filename (legal, filename);
	g_free (filename);

	/* use state object */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	/* set a cancellable, as we're using the store directly */
	cancellable = g_cancellable_new ();
	zif_state_set_cancellable (state, cancellable);
	g_object_unref (cancellable);

	groups = zif_groups_new ();
	filename = zif_test_get_data_file ("yum-comps-groups.conf");
	ret = zif_groups_set_mapping_file (groups, filename, NULL);
	g_free (filename);
	g_assert (ret);

	store = ZIF_STORE_LOCAL (zif_store_local_new ());
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);

	filename = zif_test_get_data_file ("root");
	zif_store_local_set_prefix (store, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	g_test_timer_start ();
	ret = zif_store_load (ZIF_STORE (store), state, &error);
	elapsed = g_test_timer_elapsed ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (elapsed, <, 1000);

	zif_state_reset (state);
	g_test_timer_start ();
	ret = zif_store_load (ZIF_STORE (store), state, &error);
	elapsed = g_test_timer_elapsed ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (elapsed, <, 10);

	/* resolve with just the name */
	zif_state_reset (state);
	to_array[0] = "test";
	to_array[1] = NULL;
	g_test_timer_start ();
	array = zif_store_resolve (ZIF_STORE (store), (gchar**)to_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	elapsed = g_test_timer_elapsed ();
	g_assert_cmpint (array->len, ==, 1);
	package = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (package), ==, "test;0.1-1.fc14;noarch;installed");
	g_ptr_array_unref (array);
	g_assert_cmpint (elapsed, <, 1000);

	/* resolve with name and name.arch ensuring only one package */
	zif_state_reset (state);
	to_array[0] = "test.noarch";
	to_array[1] = NULL;
	array = zif_store_resolve_full (ZIF_STORE (store),
					(gchar**)to_array,
					ZIF_STORE_RESOLVE_FLAG_USE_NAME |
					ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH,
					state,
					&error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	package = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (package), ==, "test;0.1-1.fc14;noarch;installed");
	g_ptr_array_unref (array);

	/* resolve with name globbing */
	zif_state_reset (state);
	to_array[0] = "t*";
	to_array[1] = NULL;
	array = zif_store_resolve_full (ZIF_STORE (store),
					(gchar**)to_array,
					ZIF_STORE_RESOLVE_FLAG_USE_NAME |
					ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					state,
					&error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	package = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (package), ==, "test;0.1-1.fc14;noarch;installed");
	g_ptr_array_unref (array);

	/* resolve with name-version */
	zif_state_reset (state);
	to_array[0] = "test-0.1-1.fc14";
	to_array[1] = NULL;
	array = zif_store_resolve_full (ZIF_STORE (store),
					(gchar**)to_array,
					ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION,
					state,
					&error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	package = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (package), ==, "test;0.1-1.fc14;noarch;installed");
	g_ptr_array_unref (array);

	/* resolve with name-version.arch */
	zif_state_reset (state);
	to_array[0] = "test-0.1-1.fc14.noarch";
	to_array[1] = NULL;
	array = zif_store_resolve_full (ZIF_STORE (store),
					(gchar**)to_array,
					ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION_ARCH,
					state,
					&error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	package = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_id (package), ==, "test;0.1-1.fc14;noarch;installed");
	g_ptr_array_unref (array);

	/* find package */
	zif_state_reset (state);
	package = zif_store_find_package (ZIF_STORE (store),
					  "test;0.1-1.fc14;noarch;installed",
					  state,
					  &error);
	g_assert_no_error (error);
	g_assert (package != NULL);
	g_object_unref (package);

	/* find package with repo_id suffix */
	zif_state_reset (state);
	package = zif_store_find_package (ZIF_STORE (store),
					  "test;0.1-1.fc14;noarch;installed:fedora",
					  state,
					  &error);
	g_assert_no_error (error);
	g_assert (package != NULL);
	g_object_unref (package);

	/* search name */
	zif_state_reset (state);
	to_array[0] = "te";
	to_array[1] = NULL;
	array = zif_store_search_name (ZIF_STORE (store), (gchar**)to_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);

	zif_state_reset (state);
	to_array[0] = "/usr/share/test-0.1/README";
	to_array[1] = "/usr/share/depend-0.1/README";
	to_array[2] = NULL;
	array = zif_store_search_file (ZIF_STORE (store), (gchar**)to_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);
	g_ptr_array_unref (array);

	zif_state_reset (state);
	to_array[0] = "Test package";
	to_array[1] = NULL;
	array = zif_store_search_details (ZIF_STORE (store), (gchar**)to_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);

	depend_array = zif_object_array_new ();

	zif_state_reset (state);
	depend = zif_depend_new ();
	zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_ANY);
	zif_depend_set_name (depend, "Test(Interface)");
	zif_object_array_add (depend_array, depend);
	array = zif_store_what_provides (ZIF_STORE (store), depend_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);
	g_object_unref (depend);
	g_ptr_array_set_size (depend_array, 0);

	zif_state_reset (state);
	depend = zif_depend_new ();
	zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_ANY);
	zif_depend_set_name (depend, "new-test");
	zif_object_array_add (depend_array, depend);
	array = zif_store_what_conflicts (ZIF_STORE (store), depend_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);
	g_object_unref (depend);
	g_ptr_array_set_size (depend_array, 0);

	zif_state_reset (state);
	depend = zif_depend_new ();
	zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_ANY);
	zif_depend_set_name (depend, "obsolete-package");
	zif_object_array_add (depend_array, depend);
	array = zif_store_what_obsoletes (ZIF_STORE (store), depend_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_object_unref (depend);

	g_ptr_array_unref (depend_array);

	/* get this package */
	package = g_ptr_array_index (array, 0);
	g_assert (zif_package_is_installed (package));

	package_id = zif_package_get_id (package);
	split = zif_package_id_split (package_id);
	g_assert_cmpstr (split[ZIF_PACKAGE_ID_NAME], ==, "test");
	g_strfreev (split);

	g_assert (g_str_has_suffix (zif_package_get_id (package), ";installed"));

	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_summary (package, state, NULL), ==, "Test package");

	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_license (package, state, NULL), ==, "GPLv2+");

	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_category (package, state, NULL), !=, NULL);

	g_assert (!zif_package_is_devel (package));
	g_assert (!zif_package_is_gui (package));
	g_assert (zif_package_is_installed (package));
	g_assert (zif_package_is_free (package));

	g_ptr_array_unref (array);

	g_object_unref (store);
	g_assert (store == NULL);
	g_object_unref (groups);
	g_object_unref (legal);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_store_meta_func (void)
{
	ZifStore *store;
	ZifPackage *pkg;
	gboolean ret;
	GError *error = NULL;
	gchar *filename;
	ZifState *state;
	const gchar *to_array[] = {NULL, NULL};
	GPtrArray *array;

	store = zif_store_meta_new ();
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);

	/* create virtal package to add to the store */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);
	pkg = zif_package_meta_new ();
	filename = zif_test_get_data_file ("test.spec");
	ret = zif_package_meta_set_from_filename (ZIF_PACKAGE_META(pkg), filename, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpstr (zif_package_get_id (pkg), ==, "test;0.1-1%{?dist};i386;meta");

	/* add to array */
	ret = zif_store_add_package (store, pkg, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add to array, again */
	ret = zif_store_add_package (store, pkg, &error);
	g_assert_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED);
	g_assert (!ret);
	g_clear_error (&error);

	/* ensure we can find it */
	to_array[0] = "test";
	zif_state_reset (state);
	array = zif_store_resolve (store, (gchar**) to_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);

	/* ensure we can find it */
	zif_state_reset (state);
	array = zif_store_get_packages (store, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);

	/* delete from array */
	ret = zif_store_remove_package (store, pkg, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* delete from array, again */
	ret = zif_store_remove_package (store, pkg, &error);
	g_assert_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED);
	g_assert (!ret);
	g_clear_error (&error);

	g_free (filename);
	g_object_unref (pkg);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (store);
	g_assert (store == NULL);
}

static void
zif_store_remote_func (void)
{
	const gchar *in_array[] = { NULL, NULL };
	gboolean ret;
	gchar *filename;
	gchar *filename_db;
	gchar *pidfile;
	gchar *tmp;
	GError *error = NULL;
	GPtrArray *array;
	ZifCategory *category;
	ZifConfig *config;
	ZifDownload *download;
	ZifGroups *groups;
	ZifPackage *package_tmp;
	ZifState *state;
	ZifStoreLocal *store_local;
	ZifStoreRemote *store;

	/* set this up as dummy */
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	filename_db = g_build_filename (zif_tmpdir, "history.db", NULL);
	zif_config_set_filename (config, filename, NULL);
	zif_config_set_uint (config, "metadata_expire", 0, NULL);
	zif_config_set_uint (config, "mirrorlist_expire", 0, NULL);
	zif_config_set_string (config, "history_db", filename_db, NULL);
	g_free (filename);
	g_free (filename_db);

	pidfile = g_build_filename (zif_tmpdir, "zif.lock", NULL);
	zif_config_set_string (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	filename = zif_test_get_data_file (".");
	zif_config_set_string (config, "cachedir", filename, NULL);
	g_free (filename);

	/* use state object */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	store = ZIF_STORE_REMOTE (zif_store_remote_new ());
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);

	zif_state_reset (state);
	filename = zif_test_get_data_file ("repos/fedora.repo");
	ret = zif_store_remote_set_from_file (store, filename, "fedora", state, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* setup state */
	groups = zif_groups_new ();
	g_object_add_weak_pointer (G_OBJECT (groups), (gpointer *) &groups);
	filename = zif_test_get_data_file ("yum-comps-groups.conf");
	zif_groups_set_mapping_file (groups, filename, NULL);
	g_free (filename);
	store_local = ZIF_STORE_LOCAL (zif_store_local_new ());
	filename = zif_test_get_data_file ("root");
	zif_store_local_set_prefix (store_local, filename, NULL);
	g_free (filename);

	zif_state_reset (state);
	g_assert (!zif_store_remote_is_devel (store, state, NULL));
	zif_state_reset (state);
	g_assert (zif_store_remote_get_enabled (store, state, NULL));
	g_assert_cmpstr (zif_store_get_id (ZIF_STORE (store)), ==, "fedora");

	zif_state_reset (state);
	ret = zif_store_load (ZIF_STORE (store), state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	zif_state_reset (state);
	in_array[0] = "gnome-power-manager";
	array = zif_store_resolve (ZIF_STORE (store), (gchar**)in_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

	g_ptr_array_unref (array);

	zif_state_reset (state);
	in_array[0] = "power-manager";
	array = zif_store_search_name (ZIF_STORE (store), (gchar**)in_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

	g_ptr_array_unref (array);

	zif_state_reset (state);
	in_array[0] = "browser plugin";
	array = zif_store_search_details (ZIF_STORE (store), (gchar**)in_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 0);

	g_ptr_array_unref (array);

	zif_state_reset (state);
	in_array[0] = "/usr/bin/gnome-power-manager";
	array = zif_store_search_file (ZIF_STORE (store), (gchar**)in_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

	/* check the package */
	package_tmp = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_package_get_name (package_tmp), ==,
			 "gnome-power-manager");
	g_assert_cmpint (zif_package_get_trust_kind (package_tmp), ==,
			 ZIF_PACKAGE_TRUST_KIND_PUBKEY_UNVERIFIED);

	g_ptr_array_unref (array);

	zif_state_reset (state);
	ret = zif_store_remote_set_enabled (store, FALSE, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	zif_state_reset (state);
	g_assert (!zif_store_remote_get_enabled (store, state, NULL));

	zif_state_reset (state);
	ret = zif_store_remote_set_enabled (store, TRUE, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	zif_state_reset (state);
	g_assert (zif_store_remote_get_enabled (store, state, NULL));

	zif_state_reset (state);
	array = zif_store_get_packages (ZIF_STORE (store), state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);

	g_ptr_array_unref (array);

	zif_state_reset (state);
	array = zif_store_get_categories (ZIF_STORE (store), state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >, 0);

	/* get first object */
	category = g_ptr_array_index (array, 0);

	g_assert_cmpstr (zif_category_get_parent_id (category), ==, NULL);
	g_assert_cmpstr (zif_category_get_id (category), ==, "apps");
	g_assert_cmpstr (zif_category_get_name (category), ==, "Applications");

	g_ptr_array_unref (array);

	zif_state_reset (state);
	in_array[0] = "admin-tools";
	array = zif_store_search_category (ZIF_STORE (store), (gchar**)in_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >, 0);

	g_ptr_array_unref (array);

	/* check reading config from the repo file */
	ret = zif_store_remote_get_boolean (store, "skip_if_unavailable", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check falling back to config file */
	tmp = zif_store_remote_get_string (store, "releasever", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (tmp, ==, "13");
	g_free (tmp);

	g_object_unref (store);
	g_assert (store == NULL);

	/* location does not exist */
	store = ZIF_STORE_REMOTE (zif_store_remote_new ());
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);
	zif_state_reset (state);
	filename = zif_test_get_data_file ("invalid.repo");
	ret = zif_store_remote_set_from_file (store, filename, "invalid", state, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* we want to fail the download */
	g_assert (zif_config_set_boolean (config, "network", TRUE, NULL));
	g_assert (zif_config_set_boolean (config, "skip_if_unavailable", FALSE, NULL));
	download = zif_download_new ();
	g_object_add_weak_pointer (G_OBJECT (download), (gpointer *) &download);

	zif_state_reset (state);
	in_array[0] = "/usr/bin/gnome-power-manager";
	array = zif_store_search_file (ZIF_STORE (store), (gchar**)in_array, state, &error);
	g_assert_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD);
	g_assert (array == NULL);

	g_object_unref (store);
	g_assert (store == NULL);
	g_clear_error (&error);

	/* check with invalid repomd */
	store = ZIF_STORE_REMOTE (zif_store_remote_new ());
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);
	zif_state_reset (state);
	filename = zif_test_get_data_file ("corrupt-repomd.repo");
	g_assert (filename != NULL);

	ret = zif_store_remote_set_from_file (store, filename, "corrupt-repomd", state, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* set the repomd.xml to junk */
	g_mkdir_with_parents ("../data/tests/corrupt-repomd", 0777);
	ret = g_file_set_contents ("../data/tests/corrupt-repomd/repomd.xml",
				   "<html><body><pre>invalid</pre></body></html>",
				   -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* ensure loading the metadata notices the junk data, and
	 * re-downloads the repomd.xml from the location specified in
	 * the corrupt-repomd.repo file */
	zif_state_reset (state);
	in_array[0] = "gnome-power-manager";
	array = zif_store_resolve (ZIF_STORE (store), (gchar**)in_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);

	/* check with invalid repomd */
	g_object_unref (store);
	g_assert (store == NULL);
	store = ZIF_STORE_REMOTE (zif_store_remote_new ());
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);
	zif_state_reset (state);
	filename = zif_test_get_data_file ("corrupt-repomd.repo");
	ret = zif_store_remote_set_from_file (store, filename, "corrupt-repomd", state, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* set the repomd.xml to blank */
	ret = g_file_set_contents ("../data/tests/corrupt-repomd/repomd.xml", "",
				   -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* ensure loading the metadata notices the empty file, and
	 * downloads the repomd.xml */
	zif_state_reset (state);
	array = zif_store_resolve (ZIF_STORE (store), (gchar**)in_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);
	g_object_unref (store);
	g_assert (store == NULL);

	/* start afresh */
	ret = g_spawn_command_line_sync ("rm -rf ../data/tests/corrupt-repomd/*", NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* create packages */
	ret = g_spawn_command_line_sync ("mkdir ../data/tests/corrupt-repomd/packages", NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* create dummy package */
	ret = g_spawn_command_line_sync ("touch ../data/tests/corrupt-repomd/packages/moo.rpm", NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* try to clean a blank repo */
	store = ZIF_STORE_REMOTE (zif_store_remote_new ());
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);
	zif_state_reset (state);
	filename = zif_test_get_data_file ("corrupt-repomd.repo");
	ret = zif_store_remote_set_from_file (store, filename, "corrupt-repomd", state, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	zif_state_reset (state);
	ret = zif_store_clean (ZIF_STORE (store), state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);

	/* ensure packages are gone */
	ret = g_file_test ("../data/tests/corrupt-repomd/packages/moo.rpm", G_FILE_TEST_EXISTS);
	g_assert (!ret);

	/* refresh on an empty repo */
	zif_state_reset (state);
	ret = zif_store_refresh (ZIF_STORE (store), TRUE, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);

	g_object_unref (download);
	g_assert (download == NULL);
	g_object_unref (store);
	g_assert (store == NULL);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (store_local);
	g_object_unref (groups);
	g_assert (groups == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_store_rhn_func (void)
{
	ZifStore *store;
	ZifConfig *config;
	ZifState *state;
	gboolean ret;
	GError *error = NULL;
	gchar *pidfile;
	gchar *filename;

	/* set this up as dummy */
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	zif_config_set_filename (config, filename, NULL);
	zif_config_set_uint (config, "metadata_expire", 0, NULL);
	zif_config_set_uint (config, "mirrorlist_expire", 0, NULL);
	g_free (filename);

	pidfile = g_build_filename (zif_tmpdir, "zif.lock", NULL);
	zif_config_set_string (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	/* use state object */
	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	store = zif_store_rhn_new ();
	g_object_add_weak_pointer (G_OBJECT (store), (gpointer *) &store);

	/* try to load without session key */
	zif_state_reset (state);
	ret = zif_store_load (store, state, &error);
	g_assert_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_AS_OFFLINE);
	g_assert (!ret);
	g_clear_error (&error);

	/* logout before login */
	ret = zif_store_rhn_logout (ZIF_STORE_RHN (store),
				    &error);
	g_assert_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED);
	g_assert (!ret);
	g_clear_error (&error);

	/* login without a server */
	ret = zif_store_rhn_login (ZIF_STORE_RHN (store),
				   "test",
				   "test",
				   &error);
	g_assert_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED);
	g_assert (!ret);
	g_clear_error (&error);

	/* set the server, then try again to login */
	zif_store_rhn_set_server (ZIF_STORE_RHN (store),
				  "https://rhn.redhat.com/rpc/api");
	ret = zif_store_rhn_login (ZIF_STORE_RHN (store),
				   "test",
				   "test",
				   &error);
	g_assert_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED);
	g_assert (!ret);
	g_clear_error (&error);

#if 0
	/* show the session key and version */
	version = zif_store_rhn_get_version (ZIF_STORE_RHN (store),
					     &error);
	g_assert_no_error (error);
	g_assert (version != NULL);
	g_debug ("version = '%s', session_key = %s",
		 version,
		 zif_store_rhn_get_session_key (ZIF_STORE_RHN (store)));
	g_free (version);

	/* logout */
	ret = zif_store_rhn_logout (ZIF_STORE_RHN (store),
				    &error);
	g_assert_no_error (error);
	g_assert (ret);
#endif

	g_object_unref (store);
	g_assert (store == NULL);
	g_object_unref (state);
	g_assert (state == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static void
zif_string_func (void)
{
	ZifString *string;
	string = zif_string_new ("kernel");
	g_assert_cmpstr (zif_string_get_value (string), ==, "kernel");
	zif_string_ref (string);
	zif_string_unref (string);
	g_assert_cmpstr (zif_string_get_value (string), ==, "kernel");
	string = zif_string_unref (string);
	g_assert (string == NULL);
}

static void
zif_update_func (void)
{
	ZifUpdate *update;

	update = zif_update_new ();
	g_assert (update != NULL);

	g_object_unref (update);
}

static void
zif_update_info_func (void)
{
	ZifUpdateInfo *update_info;

	update_info = zif_update_info_new ();
	g_assert (update_info != NULL);

	g_object_unref (update_info);
}

static void
zif_utils_func (void)
{
	const gchar *e;
	const gchar *package_id_const = "totem;0.1.2;i386;fedora";
	const gchar *r;
	const gchar *v;
	const gchar *d;
	const guint iterations = 100000;
	gboolean ret;
	gchar *evr;
	gchar *filename;
	gchar *filename_tmp;
	gchar *filename_gpg;
	gchar *name;
	gchar *package_id;
	gchar *sn, *sv, *sr, *sa;
	gchar **split;
	gdouble time_iter;
	gdouble time_split;
	GError *error = NULL;
	GString *str;
	GTimer *timer;
	guint i;
	guint se;
	ZifState *state;

	state = zif_state_new ();
	g_object_add_weak_pointer (G_OBJECT (state), (gpointer *) &state);

	package_id = zif_package_id_from_nevra ("kernel", 0, "0.1.0", "1", "i386", "fedora");
	g_assert_cmpstr (package_id, ==, "kernel;0.1.0-1;i386;fedora");
	g_free (package_id);

	package_id = zif_package_id_from_nevra ("kernel", 2, "0.1.0", "1", "i386", "fedora");
	g_assert_cmpstr (package_id, ==, "kernel;2:0.1.0-1;i386;fedora");
	g_free (package_id);

	ret = zif_init ();
	g_assert (ret);

	g_assert (zif_boolean_from_text ("1"));
	g_assert (zif_boolean_from_text ("TRUE"));
	g_assert (!zif_boolean_from_text ("false"));
	g_assert (!zif_boolean_from_text (""));

	evr = g_strdup ("7:1.0.0-6");
	zif_package_convert_evr (evr, &e, &v, &r);
	g_assert_cmpstr (e, ==, "7");
	g_assert_cmpstr (v, ==, "1.0.0");
	g_assert_cmpstr (r, ==, "6");
	g_free (evr);

	/* no epoch */
	evr = g_strdup ("1.0.0-6");
	zif_package_convert_evr (evr, &e, &v, &r);
	g_assert (e == NULL);
	g_assert_cmpstr (v, ==, "1.0.0");
	g_assert_cmpstr (r, ==, "6");
	g_free (evr);

	/* with distro-release (compat) */
	evr = g_strdup ("1.0.0-6.fc15");
	zif_package_convert_evr (evr, &e, &v, &r);
	g_assert (e == NULL);
	g_assert_cmpstr (v, ==, "1.0.0");
	g_assert_cmpstr (r, ==, "6.fc15");
	g_free (evr);

	/* with distro-release */
	evr = g_strdup ("1.0.0-6.fc15");
	zif_package_convert_evr_full (evr, &e, &v, &r, &d);
	g_assert (e == NULL);
	g_assert_cmpstr (v, ==, "1.0.0");
	g_assert_cmpstr (r, ==, "6");
	g_assert_cmpstr (d, ==, "fc15");
	g_free (evr);

	/* no epoch or release */
	evr = g_strdup ("1.0.0");
	zif_package_convert_evr (evr, &e, &v, &r);
	g_assert (e == NULL);
	g_assert_cmpstr (v, ==, "1.0.0");
	g_assert (r == NULL);
	g_free (evr);

	g_assert (zif_compare_evr ("1:1.0.2-3", "1:1.0.2-3") == 0);
	g_assert (zif_compare_evr ("1:1.0.2-3", "1:1.0.2-4") == -1);
	g_assert (zif_compare_evr ("1:1.0.2-4", "1:1.0.2-3") == 1);
	g_assert (zif_compare_evr ("1:0.1.0-1", "1.0.2-2") == 1);
	g_assert (zif_compare_evr ("1.0.2-1", "1.0.1-1") == 1);
	g_assert (zif_compare_evr ("0.0.1-2", "0:0.0.1-2") == 0);
	g_assert (zif_compare_evr ("0:0.0.1-2", "0.0.1-2") == 0);
	g_assert (zif_compare_evr ("0.1", "0:0.1-1") == 0);
	g_assert (zif_compare_evr ("0.1", "0.1-1.fc15") == 0);
	g_assert (zif_compare_evr ("0.5.8-1.fc15", "0.5.8") == 0);

	filename = zif_file_get_uncompressed_name ("/dave/moo.sqlite.gz");
	g_assert_cmpstr (filename, ==, "/dave/moo.sqlite");
	g_free (filename);

	filename = zif_file_get_uncompressed_name ("/dave/moo.sqlite");
	g_assert_cmpstr (filename, ==, "/dave/moo.sqlite");
	g_free (filename);

	filename = zif_test_get_data_file ("compress.txt.gz");
	filename_tmp = g_build_filename (zif_tmpdir, "comps-fedora.xml", NULL);
	ret = zif_file_decompress (filename, filename_tmp, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);
	g_free (filename_tmp);

	filename = zif_test_get_data_file ("compress.txt.bz2");
	filename_tmp = g_build_filename (zif_tmpdir, "moo.sqlite", NULL);
	ret = zif_file_decompress (filename, filename_tmp, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);
	g_free (filename_tmp);

	filename = zif_test_get_data_file ("compress.txt.lzma");
	filename_tmp = g_build_filename (zif_tmpdir, "comps-fedora.xml", NULL);
	ret = zif_file_decompress (filename, filename_tmp, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);
	g_free (filename_tmp);

	filename = zif_test_get_data_file ("compress.txt.xz");
	filename_tmp = g_build_filename (zif_tmpdir, "comps-fedora.xml", NULL);
	ret = zif_file_decompress (filename, filename_tmp, state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);
	g_free (filename_tmp);

	g_assert_cmpint (zif_time_string_to_seconds (""), ==, 0);
	g_assert_cmpint (zif_time_string_to_seconds ("10"), ==, 0);
	g_assert_cmpint (zif_time_string_to_seconds ("10f"), ==, 0);
	g_assert_cmpint (zif_time_string_to_seconds ("10s"), ==, 10);
	g_assert_cmpint (zif_time_string_to_seconds ("10m"), ==, 600);
	g_assert_cmpint (zif_time_string_to_seconds ("10h"), ==, 36000);
	g_assert_cmpint (zif_time_string_to_seconds ("10d"), ==, 864000);

	/* get the time it takes to split a million strings */
	timer = g_timer_new ();
	for (i = 0; i < iterations; i++) {
		split = zif_package_id_split (package_id_const);
		g_strfreev (split);
	}
	time_split = g_timer_elapsed (timer, NULL);

	/* get the time it takes to iterate a million strings */
	g_timer_reset (timer);
	for (i = 0; i < iterations; i++) {
		name = zif_package_id_get_name (package_id_const);
		g_free (name);
	}
	time_iter = g_timer_elapsed (timer, NULL);

	/* ensure iter is faster by at least 4 times */
	g_assert_cmpfloat (time_iter * 4, <, time_split);

	g_timer_destroy (timer);
	g_object_unref (state);
	g_assert (state == NULL);

	/* test GPGME functionality */
	filename = zif_test_get_data_file ("signed-metadata/repomd.xml");
	filename_gpg = zif_test_get_data_file ("signed-metadata/repomd.xml.asc");
	g_assert (filename != NULL);
	g_assert (filename_gpg != NULL);
	ret = zif_utils_gpg_verify (filename, filename_gpg, &error);
	g_assert_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED);
	g_assert (!ret);
	g_error_free (error);
	g_free (filename);
	g_free (filename_gpg);

	/* verify with epoch */
	ret = zif_package_id_to_nevra ("kernel;4:0.1-5.fc4;i386;fedora", &sn, &se, &sv, &sr, &sa);
	g_assert (ret);
	g_assert_cmpstr (sn, ==, "kernel");
	g_assert_cmpint (se, ==, 4);
	g_assert_cmpstr (sv, ==, "0.1");
	g_assert_cmpstr (sr, ==, "5.fc4");
	g_assert_cmpstr (sa, ==, "i386");
	g_free (sn); g_free (sv); g_free (sr); g_free (sa);

	/* verify without epoch */
	ret = zif_package_id_to_nevra ("kernel;0.1-5.fc4;i386;fedora", &sn, &se, &sv, &sr, &sa);
	g_assert (ret);
	g_assert_cmpstr (sn, ==, "kernel");
	g_assert_cmpint (se, ==, 0);
	g_assert_cmpstr (sv, ==, "0.1");
	g_assert_cmpstr (sr, ==, "5.fc4");
	g_assert_cmpstr (sa, ==, "i386");
	g_free (sn); g_free (sv); g_free (sr); g_free (sa);

	/* verify with invalid version */
	ret = zif_package_id_to_nevra ("kernel;0.1;i386;fedora", &sn, &se, &sv, &sr, &sa);
	g_assert (!ret);

	/* test string replacement */
	str = g_string_new ("We would like to go to go!");

	/* nothing */
	zif_string_replace (str, "tree", "want");
	g_assert_cmpstr (str->str, ==, "We would like to go to go!");
	g_assert_cmpint (str->len, ==, strlen (str->str));

	/* no replacement text */
	zif_string_replace (str, "We ", "");
	g_assert_cmpstr (str->str, ==, "would like to go to go!");
	g_assert_cmpint (str->len, ==, strlen (str->str));

	/* same */
	zif_string_replace (str, "like", "want");
	g_assert_cmpstr (str->str, ==, "would want to go to go!");
	g_assert_cmpint (str->len, ==, strlen (str->str));

	/* shorter */
	zif_string_replace (str, "to go", "it");
	g_assert_cmpstr (str->str, ==, "would want it it!");
	g_assert_cmpint (str->len, ==, strlen (str->str));

	/* longer */
	zif_string_replace (str, "would", "should not");
	g_assert_cmpstr (str->str, ==, "should not want it it!");
	g_assert_cmpint (str->len, ==, strlen (str->str));

	/* multiple */
	zif_string_replace (str, " ", "_");
	g_assert_cmpstr (str->str, ==, "should_not_want_it_it!");
	g_assert_cmpint (str->len, ==, strlen (str->str));

	/* less, on the end */
	zif_string_replace (str, "it_it!", "it!");
	g_assert_cmpstr (str->str, ==, "should_not_want_it!");
	g_assert_cmpint (str->len, ==, strlen (str->str));

	/* more, on the end */
	zif_string_replace (str, "it!", "it_it_it!");
	g_assert_cmpstr (str->str, ==, "should_not_want_it_it_it!");
	g_assert_cmpint (str->len, ==, strlen (str->str));

	g_string_free (str, TRUE);
}

static void
zif_history_func (void)
{
	GArray *transactions;
	gboolean ret;
	gchar *filename;
	gchar *filename_db;
	gchar *tmp;
	GError *error = NULL;
	GPtrArray *packages;
	gint64 timestamp;
	guint uid;
	ZifConfig *config;
	ZifDb *db;
	ZifHistory *history;
	ZifPackage *package1;
	ZifPackage *package2;
	ZifPackage *package3;
	ZifPackage *package_tmp;

	/* set this up as dummy */
	config = zif_config_new ();
	g_object_add_weak_pointer (G_OBJECT (config), (gpointer *) &config);
	filename = zif_test_get_data_file ("zif.conf");
	filename_db = g_build_filename (zif_tmpdir, "history.db", NULL);
	zif_config_set_filename (config, filename, NULL);
	zif_config_set_uint (config, "metadata_expire", 0, NULL);
	zif_config_set_uint (config, "mirrorlist_expire", 0, NULL);
	zif_config_set_string (config, "history_db", filename_db, NULL);
	g_free (filename);
	g_free (filename_db);

	/* create a new file */
	history = zif_history_new ();
	g_object_add_weak_pointer (G_OBJECT (history), (gpointer *) &history);

	/* add an entry */
	timestamp = g_get_real_time ();
	package1 = zif_package_new ();
	ret = zif_package_set_id (package1,
				  "hal;0.1-1.fc13;i386;fedora",
				  &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = zif_history_add_entry (history,
				     package1,
				     timestamp,
				     ZIF_TRANSACTION_REASON_UPDATE_FOR_CONFLICT,
				     1000,
				     "install hal-info",
				     &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add another entry */
	package2 = zif_package_new ();
	ret = zif_package_set_id (package2,
				  "upower;0.1-1.fc13;i386;fedora",
				  &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = zif_history_add_entry (history,
				     package2,
				     timestamp,
				     ZIF_TRANSACTION_REASON_INSTALL_FOR_UPDATE,
				     500,
				     "update upower-devel",
				     &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* don't add this, used for checking error */
	package3 = zif_package_new ();
	ret = zif_package_set_id (package3,
				  "PackageKit-glib-devel;0.6.9-4.fc14;i686;installed",
				  &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get all transactions */
	transactions = zif_history_list_transactions (history, &error);
	g_assert_no_error (error);
	g_assert (transactions != NULL);
	g_assert_cmpint (transactions->len, ==, 1);
	g_assert_cmpint (g_array_index (transactions, gint64, 0) - timestamp, <, 10);

	/* get both packages */
	packages = zif_history_get_packages (history,
					     timestamp,
					     &error);
	g_assert_no_error (error);
	g_assert (packages != NULL);
	g_assert_cmpint (packages->len, ==, 2);
	package_tmp = g_ptr_array_index (packages, 0);
	g_assert_cmpstr (zif_package_get_id (package_tmp), ==,
			 "hal;0.1-1.fc13;i386;fedora");
	package_tmp = g_ptr_array_index (packages, 1);
	g_assert_cmpstr (zif_package_get_id (package_tmp), ==,
			 "upower;0.1-1.fc13;i386;fedora");
	g_ptr_array_unref (packages);

	/* get uid */
	uid = zif_history_get_uid (history, package1, timestamp, &error);
	g_assert_no_error (error);
	g_assert_cmpint (uid, ==, 1000);

	/* get cmdline */
	tmp = zif_history_get_cmdline (history, package1, timestamp, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (tmp, ==, "install hal-info");
	g_free (tmp);

	/* get repo */
	tmp = zif_history_get_repo (history, package1, timestamp, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (tmp, ==, "fedora");
	g_free (tmp);

	/* get repo */
	tmp = zif_history_get_repo (history, package3, timestamp, &error);
	g_assert_error (error,
			ZIF_HISTORY_ERROR,
			ZIF_HISTORY_ERROR_FAILED);
	g_assert_cmpstr (tmp, ==, NULL);
	g_clear_error (&error);

	/* get repo newest */
	tmp = zif_history_get_repo_newest (history, package1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (tmp, ==, "fedora");
	g_free (tmp);

	/* create a dummy database */
	db = zif_db_new ();
	g_object_add_weak_pointer (G_OBJECT (db), (gpointer *) &db);
	ret = zif_db_set_root (db, "../data/tests/yumdb", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check import */
	ret = zif_history_import (history, db, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get repo newest */
	tmp = zif_history_get_repo_newest (history, package3, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (tmp, ==, "fedora");
	g_free (tmp);

	/* check uid */
	uid = zif_history_get_uid (history, package3, 1287927872000000, &error);
	g_assert_no_error (error);
	g_assert_cmpint (uid, ==, 500);

	g_object_unref (package1);
	g_object_unref (package2);
	g_object_unref (package3);
	g_object_unref (history);
	g_assert (history == NULL);
	g_object_unref (db);
	g_assert (db == NULL);
	g_object_unref (config);
	g_assert (config == NULL);
}

static gboolean
zif_self_test_remove_recursive (const gchar *directory)
{
	const gchar *filename;
	gboolean ret = FALSE;
	gchar *src;
	GDir *dir;
	GError *error = NULL;
	gint retval;

	/* try to open */
	dir = g_dir_open (directory, 0, &error);
	if (dir == NULL) {
		g_warning ("cannot open directory: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		src = g_build_filename (directory, filename, NULL);
		ret = g_file_test (src, G_FILE_TEST_IS_DIR);
		if (ret) {
			zif_self_test_remove_recursive (src);
			retval = g_remove (src);
			if (retval != 0)
				g_warning ("failed to delete %s", src);
		} else {
			retval = g_unlink (src);
			if (retval != 0)
				g_warning ("failed to delete %s", src);
		}
		g_free (src);
	}
	g_dir_close (dir);
	ret = TRUE;
out:
	return ret;
}

static gchar *
zif_self_test_get_tmpdir (void)
{
	gchar *tmpdir;
	GError *error = NULL;
	tmpdir = g_dir_make_tmp ("zif-self-test-XXXXXX", &error);
	if (tmpdir == NULL) {
		g_warning ("failed to get a tempdir: %s", error->message);
		g_error_free (error);
	}
	return tmpdir;
}

int
main (int argc, char **argv)
{
	gint retval;

	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* create a new tempdir */
	zif_tmpdir = zif_self_test_get_tmpdir ();
	g_debug ("Created scratch area %s", zif_tmpdir);

	/* tests go here */
	g_test_add_func ("/zif/utils", zif_utils_func);
	g_test_add_func ("/zif/state", zif_state_func);
	g_test_add_func ("/zif/state[child]", zif_state_child_func);
	g_test_add_func ("/zif/state[parent-1-step]", zif_state_parent_one_step_proxy_func);
	g_test_add_func ("/zif/state[no-equal]", zif_state_non_equal_steps_func);
	g_test_add_func ("/zif/state[no-progress]", zif_state_no_progress_func);
	g_test_add_func ("/zif/state[finish]", zif_state_finish_func);
	g_test_add_func ("/zif/state[error-handler]", zif_state_error_handler_func);
	g_test_add_func ("/zif/state[speed]", zif_state_speed_func);
	g_test_add_func ("/zif/state[locking]", zif_state_locking_func);
	g_test_add_func ("/zif/state[finished]", zif_state_finished_func);
	g_test_add_func ("/zif/changeset", zif_changeset_func);
	g_test_add_func ("/zif/config", zif_config_func);
	g_test_add_func ("/zif/config[changed]", zif_config_changed_func);
	g_test_add_func ("/zif/db", zif_db_func);
	g_test_add_func ("/zif/depend", zif_depend_func);
	g_test_add_func ("/zif/download", zif_download_func);
	g_test_add_func ("/zif/groups", zif_groups_func);
	g_test_add_func ("/zif/history", zif_history_func);
	g_test_add_func ("/zif/legal", zif_legal_func);
	g_test_add_func ("/zif/lock", zif_lock_func);
	g_test_add_func ("/zif/lock[threads]", zif_lock_threads_func);
	g_test_add_func ("/zif/manifest", zif_manifest_func);
	g_test_add_func ("/zif/md", zif_md_func);
	g_test_add_func ("/zif/md-comps", zif_md_comps_func);
	g_test_add_func ("/zif/md-delta", zif_md_delta_func);
	g_test_add_func ("/zif/md-filelists-sql", zif_md_filelists_sql_func);
	g_test_add_func ("/zif/md-filelists-xml", zif_md_filelists_xml_func);
	g_test_add_func ("/zif/md-metalink", zif_md_metalink_func);
	g_test_add_func ("/zif/md-mirrorlist", zif_md_mirrorlist_func);
	g_test_add_func ("/zif/md-other-sql", zif_md_other_sql_func);
	g_test_add_func ("/zif/md-primary-sql", zif_md_primary_sql_func);
	g_test_add_func ("/zif/md-primary-xml", zif_md_primary_xml_func);
	g_test_add_func ("/zif/md-updateinfo", zif_md_updateinfo_func);
	g_test_add_func ("/zif/monitor", zif_monitor_func);
	g_test_add_func ("/zif/package-local", zif_package_local_func);
	g_test_add_func ("/zif/package-remote", zif_package_remote_func);
	g_test_add_func ("/zif/package-meta", zif_package_meta_func);
	g_test_add_func ("/zif/package", zif_package_func);
	g_test_add_func ("/zif/package-array", zif_package_array_func);
	g_test_add_func ("/zif/release", zif_release_func);
	g_test_add_func ("/zif/repos", zif_repos_func);
	g_test_add_func ("/zif/store-local", zif_store_local_func);
	g_test_add_func ("/zif/store-meta", zif_store_meta_func);
	g_test_add_func ("/zif/store-remote", zif_store_remote_func);
	g_test_add_func ("/zif/store-directory", zif_store_directory_func);
	g_test_add_func ("/zif/store-rhn", zif_store_rhn_func);
	g_test_add_func ("/zif/string", zif_string_func);
	g_test_add_func ("/zif/transaction", zif_transaction_func);
	g_test_add_func ("/zif/update-info", zif_update_info_func);
	g_test_add_func ("/zif/update", zif_update_func);

	/* go go go! */
	retval = g_test_run ();

	/* wipe the tempdir */
	g_debug ("Removing scratch area %s", zif_tmpdir);
	zif_self_test_remove_recursive (zif_tmpdir);
	g_remove (zif_tmpdir);
	g_free (zif_tmpdir);
	return retval;
}

