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
#include "zif-changeset.h"
#include "zif-config.h"
#include "zif-delta.h"
#include "zif-depend.h"
#include "zif-download.h"
#include "zif-groups.h"
#include "zif.h"
#include "zif-legal.h"
#include "zif-lock.h"
#include "zif-md.h"
#include "zif-md-comps.h"
#include "zif-md-delta.h"
#include "zif-md-filelists-sql.h"
#include "zif-md-filelists-xml.h"
#include "zif-md-metalink.h"
#include "zif-md-mirrorlist.h"
#include "zif-md-other-sql.h"
#include "zif-md-primary-sql.h"
#include "zif-md-primary-xml.h"
#include "zif-md-updateinfo.h"
#include "zif-media.h"
#include "zif-monitor.h"
#include "zif-package.h"
#include "zif-package-local.h"
#include "zif-package-remote.h"
#include "zif-repos.h"
#include "zif-state.h"
#include "zif-store-array.h"
#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-store-remote.h"
#include "zif-string.h"
#include "zif-update.h"
#include "zif-update-info.h"
#include "zif-utils.h"

/**
 * zif_test_get_data_file:
 **/
static gchar *
zif_test_get_data_file (const gchar *filename)
{
	gboolean ret;
	gchar *full;

	/* check to see if we are being run in the build root */
	full = g_build_filename ("..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret)
		return full;
	g_free (full);

	/* check to see if we are being run in make check */
	full = g_build_filename ("..", "..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret)
		return full;
	g_free (full);
	return NULL;
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
	g_assert_cmpstr (zif_changeset_get_author (changeset), ==, "Milan Crha <mcrha at redhat.com>");
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
	g_assert (config != NULL);

	filename = zif_test_get_data_file ("yum.conf");
	ret = zif_config_set_filename (config, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	value = zif_config_get_string (config, "cachedir", NULL);
	g_assert_cmpstr (value, ==, "/var/cache/yum");
	g_free (value);

	value = zif_config_get_string (config, "notgoingtoexists", NULL);
	g_assert_cmpstr (value, ==, NULL);
	g_free (value);

	ret = zif_config_get_boolean (config, "exactarch", NULL);
	g_assert (ret);

	ret = zif_config_set_local (config, "cachedir", "/tmp/cache", NULL);
	g_assert (ret);

	ret = zif_config_set_local (config, "cachedir", "/tmp/cache", NULL);
	g_assert (!ret);

	value = zif_config_get_string (config, "cachedir", NULL);
	g_assert_cmpstr (value, ==, "/tmp/cache");
	g_free (value);

	ret = zif_config_reset_default (config, NULL);
	g_assert (ret);

	value = zif_config_get_string (config, "cachedir", NULL);
	g_assert_cmpstr (value, ==, "/var/cache/yum");
	g_free (value);

	value = zif_config_expand_substitutions (config, "http://fedora/4/6/moo.rpm", NULL);
	g_assert_cmpstr (value, ==, "http://fedora/4/6/moo.rpm");
	g_free (value);

	array = zif_config_get_basearch_array (config);
	len = g_strv_length (array);
	basearch = zif_config_get_string (config, "basearch", NULL);
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
}

static void
zif_depend_func (void)
{
	ZifDepend *depend;

	depend = zif_depend_new ("kernel", ZIF_DEPEND_FLAG_GREATER, "2.6.0");
	g_assert_cmpstr (depend->name, ==, "kernel");
	g_assert_cmpint (depend->count, ==, 1);

	zif_depend_ref (depend);
	g_assert (depend->count == 2);

	zif_depend_unref (depend);
	g_assert (depend->count == 1);

	depend = zif_depend_unref (depend);
	g_assert (depend == NULL);
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
	GCancellable *cancellable;
	gboolean ret;
	GError *error = NULL;

	download = zif_download_new ();
	g_assert (download != NULL);
	g_assert (zif_download_set_proxy (download, NULL, NULL));

	state = zif_state_new ();
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_download_progress_changed), NULL);
	cancellable = zif_state_get_cancellable (state);

	g_cancellable_cancel (cancellable);

	ret = zif_download_file (download, "http://people.freedesktop.org/~hughsient/temp/Screenshot.png",
				 g_get_tmp_dir (), state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (_updates, >, 5);

	/* setup cancel */
	g_thread_create ((GThreadFunc) zif_download_cancel_thread_cb, cancellable, FALSE, NULL);

	zif_state_reset (state);
	ret = zif_download_file (download, "http://people.freedesktop.org/~hughsient/temp/Screenshot.png",
				 g_get_tmp_dir (), state, &error);
	g_assert (!ret);

	g_object_unref (download);
	g_object_unref (state);
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

	groups = zif_groups_new ();
	g_assert (groups != NULL);

	filename = zif_test_get_data_file ("yum-comps-groups.conf");
	ret = zif_groups_set_mapping_file (groups, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	ret = zif_groups_load (groups, &error);
	g_assert_no_error (error);
	g_assert (ret);

	array = zif_groups_get_groups (groups, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	group = g_ptr_array_index (array, 0);
	g_assert_cmpstr (group, ==, "admin-tools");

	array = zif_groups_get_categories (groups, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >, 100);
	g_ptr_array_unref (array);

	group = zif_groups_get_group_for_cat (groups, "language-support;kashubian-support", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (group, ==, "localization");

	array = zif_groups_get_cats_for_group (groups, "localization", &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >, 50);
	category = g_ptr_array_index (array, 0);
	g_assert_cmpstr (category, ==, "base-system;input-methods");
	g_ptr_array_unref (array);

	g_object_unref (groups);
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

static void
zif_lock_func (void)
{
	ZifLock *lock;
	ZifConfig *config;
	gboolean ret;
	GError *error = NULL;
	gchar *pidfile;
	guint pid = 0;
	gchar *filename;

	config = zif_config_new ();
	g_assert (config != NULL);

	filename = zif_test_get_data_file ("yum.conf");
	ret = zif_config_set_filename (config, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	lock = zif_lock_new ();
	g_assert (lock != NULL);

	/* set this to somewhere we can write to */
	pidfile = g_build_filename (g_get_tmp_dir (), "zif.lock", NULL);
	zif_config_set_local (config, "pidfile", pidfile, NULL);
	g_assert_cmpstr (pidfile, ==, "/tmp/zif.lock");

	/* remove file */
	g_unlink (pidfile);
	g_assert (!zif_lock_is_locked (lock, &pid));
	g_assert (!zif_lock_set_unlocked (lock, NULL));
	ret = zif_lock_set_locked (lock, &pid, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_lock_is_locked (lock, &pid));

	/* ensure pid is us */
	g_assert ((pid == (guint)getpid ()));
	g_assert (zif_lock_set_unlocked (lock, NULL));
	g_assert (!zif_lock_set_unlocked (lock, NULL));

	g_object_unref (lock);
	g_object_unref (config);
	g_free (pidfile);
}

static void
zif_md_func (void)
{
	ZifMd *md;
	gboolean ret;
	GError *error = NULL;
	ZifState *state;

	state = zif_state_new ();

	md = zif_md_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	/* you can't load a baseclass */
	zif_md_set_id (md, "old-name-no-error");
	zif_md_set_id (md, "fedora");
	ret = zif_md_load (md, state, &error);
	g_assert_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_NO_SUPPORT);
	g_assert (!ret);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	g_object_unref (md);
	g_object_unref (state);
}

static void
zif_md_comps_func (void)
{
	ZifMdComps *md;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *id;
	ZifState *state;
	ZifCategory *category;
	gchar *filename;

	state = zif_state_new ();

	md = zif_md_comps_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	filename = zif_test_get_data_file ("fedora/comps-fedora.xml.gz");
	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_COMPS);
	zif_md_set_filename (ZIF_MD (md), filename);
	zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	zif_md_set_checksum (ZIF_MD (md), "02493204cfd99c1cab1c812344dfebbeeadbe0ae04ace5ad338e1d045dd564f1");
	zif_md_set_checksum_uncompressed (ZIF_MD (md), "1523fcdb34bb65f9f0964176d00b8ea6590febddb54521bf289f0d22e86d5fca");
	g_free (filename);

	array = zif_md_comps_get_categories (md, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));

	category = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_category_get_id (category), ==, "apps");
	g_assert_cmpstr (zif_category_get_name (category), ==, "Applications");
	g_assert_cmpstr (zif_category_get_summary (category), ==, "Applications to perform a variety of tasks");
	g_ptr_array_unref (array);

	zif_state_reset (state);
	array = zif_md_comps_get_groups_for_category (md, "apps", state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);

	category = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_category_get_id (category), ==, "admin-tools");
	g_ptr_array_unref (array);

	zif_state_reset (state);
	array = zif_md_comps_get_packages_for_group (md, "admin-tools", state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);

	/* and with full category id */
	zif_state_reset (state);
	array = zif_md_comps_get_packages_for_group (md, "apps;admin-tools", state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);

	id = g_ptr_array_index (array, 0);
	g_assert_cmpstr (id, ==, "test");
	g_ptr_array_unref (array);

	g_object_unref (md);
	g_object_unref (state);
}

static void
zif_md_filelists_sql_func (void)
{
	ZifMdFilelistsSql *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *pkgid;
	ZifState *state;
	const gchar *data[] = { "/usr/bin/gnome-power-manager", NULL };
	gchar *filename;

	state = zif_state_new ();

	md = zif_md_filelists_sql_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_FILELISTS_SQL);
	zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	zif_md_set_checksum (ZIF_MD (md), "5a4b8374034cbf3e6ac654c19a613d74318da890bf22ebef3d2db90616dc5377");
	zif_md_set_checksum_uncompressed (ZIF_MD (md), "498cd5a1abe685bb0bae6dab92b518649f62decfe227c28e810981f1126a2a5a");
	filename = zif_test_get_data_file ("fedora/filelists.sqlite.bz2");
	zif_md_set_filename (ZIF_MD (md), filename);
	g_free (filename);
	ret = zif_md_load (ZIF_MD (md), state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));

	zif_state_reset (state);
	array = zif_md_search_file (ZIF_MD (md), (gchar**)data, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert (array->len == 1);

	pkgid = g_ptr_array_index (array, 0);
	g_assert_cmpstr (pkgid, ==, "888f5500947e6dafb215aaf4ca0cb789a12dab404397f2a37b3623a25ed72794");
	g_assert_cmpint (strlen (pkgid), ==, 64);
	g_ptr_array_unref (array);

	g_object_unref (md);
	g_object_unref (state);
}

static void
zif_md_filelists_xml_func (void)
{
	ZifMdFilelistsXml *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifState *state;
	gchar *pkgid;
	const gchar *data[] = { "/usr/lib/debug/usr/bin/gpk-prefs.debug", NULL };
	gchar *filename;

	state = zif_state_new ();

	md = zif_md_filelists_xml_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_FILELISTS_XML);
	zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	zif_md_set_checksum (ZIF_MD (md), "cadb324b10d395058ed22c9d984038927a3ea4ff9e0e798116be44b0233eaa49");
	zif_md_set_checksum_uncompressed (ZIF_MD (md), "8018e177379ada1d380b4ebf800e7caa95ff8cf90fdd6899528266719bbfdeab");
	filename = zif_test_get_data_file ("fedora/filelists.xml.gz");
	zif_md_set_filename (ZIF_MD (md), filename);
	g_free (filename);
	ret = zif_md_load (ZIF_MD (md), state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));

	zif_state_reset (state);
	array = zif_md_search_file (ZIF_MD (md), (gchar**)data, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

	pkgid = g_ptr_array_index (array, 0);
	g_assert_cmpstr (pkgid, ==, "cec62d49c26d27b8584112d7d046782c578a097b81fe628d269d8afd7f1d54f4");
	g_ptr_array_unref (array);

	g_object_unref (state);
	g_object_unref (md);
}

static void
zif_md_metalink_func (void)
{
	ZifMdMetalink *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *uri;
	ZifState *state;
	ZifConfig *config;
	gchar *filename;

	state = zif_state_new ();
	config = zif_config_new ();
	filename = zif_test_get_data_file ("yum.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	md = zif_md_metalink_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_METALINK);
	filename = zif_test_get_data_file ("metalink.xml");
	zif_md_set_filename (ZIF_MD (md), filename);
	g_free (filename);
	ret = zif_md_load (ZIF_MD (md), state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));

	zif_state_reset (state);
	array = zif_md_metalink_get_uris (md, 50, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 43);

	uri = g_ptr_array_index (array, 0);
	g_assert_cmpstr (uri, ==, "http://www.mirrorservice.org/sites/download.fedora.redhat.com/pub/fedora/linux/development/13/i386/os/");
	g_ptr_array_unref (array);

	g_object_unref (md);
	g_object_unref (state);
	g_object_unref (config);
}

static void
zif_md_mirrorlist_func (void)
{
	ZifMdMirrorlist *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *uri;
	ZifState *state;
	ZifConfig *config;
	gchar *filename;
	gchar *basearch;

	state = zif_state_new ();
	config = zif_config_new ();
	filename = zif_test_get_data_file ("yum.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	md = zif_md_mirrorlist_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_MIRRORLIST);
	filename = zif_test_get_data_file ("mirrorlist.txt");
	zif_md_set_filename (ZIF_MD (md), filename);
	g_free (filename);
	ret = zif_md_load (ZIF_MD (md), state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));

	zif_state_reset (state);
	array = zif_md_mirrorlist_get_uris (md, state, &error);
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
	g_object_unref (config);
}

static void
zif_md_other_sql_func (void)
{
	ZifMdOtherSql *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifChangeset *changeset;
	ZifState *state;
	gchar *filename;

	state = zif_state_new ();

	md = zif_md_other_sql_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_OTHER_SQL);
	filename = zif_test_get_data_file ("fedora/other.sqlite.bz2");
	zif_md_set_filename (ZIF_MD (md), filename);
	g_free (filename);
	zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	zif_md_set_checksum (ZIF_MD (md), "b3ea68a8eed49d16ffaf9eb486095e15641fb43dcd33ef2424fbeed27adc416b");
	zif_md_set_checksum_uncompressed (ZIF_MD (md), "08df4b69b8304e24f17cb17d22f2fa328511eacad91ce5b92c03d7acb94c41d7");
	ret = zif_md_load (ZIF_MD (md), state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));

	zif_state_reset (state);
	array = zif_md_get_changelog (ZIF_MD (md),
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
	g_object_unref (md);
}

static void
zif_md_primary_sql_func (void)
{
	ZifMdPrimarySql *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifPackage *package;
	ZifState *state;
	const gchar *data[] = { "gnome-power-manager", "gnome-color-manager", NULL };
	gchar *filename;

	state = zif_state_new ();

	md = zif_md_primary_sql_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_PRIMARY_SQL);
	zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	zif_md_set_checksum (ZIF_MD (md), "5fc0d46554ca677568efdb601181f45e348c969e2aa1fcaf559f6597304a90b0");
	zif_md_set_checksum_uncompressed (ZIF_MD (md), "463c0279007959629293cdeda33ad30faf4d8c4ed0124c7c29cf895e4d07476d");
	filename = zif_test_get_data_file ("fedora/primary.sqlite.bz2");
	zif_md_set_filename (ZIF_MD (md), filename);
	g_free (filename);
	ret = zif_md_load (ZIF_MD (md), state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));

	zif_state_reset (state);
	array = zif_md_resolve (ZIF_MD (md), (gchar**)data, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert (array->len == 1);

	package = g_ptr_array_index (array, 0);
	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_summary (package, state, NULL), ==, "GNOME power management service");
	g_ptr_array_unref (array);

	g_object_unref (state);
	g_object_unref (md);
}

static void
zif_md_primary_xml_func (void)
{
	ZifMdPrimaryXml *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifPackage *package;
	ZifState *state;
	const gchar *data[] = { "gnome-power-manager", NULL };
	gchar *filename;

	state = zif_state_new ();

	md = zif_md_primary_xml_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_PRIMARY_XML);
	zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	zif_md_set_checksum (ZIF_MD (md), "33a0eed8e12f445618756b18aa49d05ee30069d280d37b03a7a15d1ec954f833");
	zif_md_set_checksum_uncompressed (ZIF_MD (md), "52e4c37b13b4b23ae96432962186e726550b19e93cf3cbf7bf55c2a673a20086");
	filename = zif_test_get_data_file ("fedora/primary.xml.gz");
	zif_md_set_filename (ZIF_MD (md), filename);
	g_free (filename);
	ret = zif_md_load (ZIF_MD (md), state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));

	zif_state_reset (state);
	array = zif_md_resolve (ZIF_MD (md), (gchar**)data, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert (array->len == 1);

	package = g_ptr_array_index (array, 0);
	zif_state_reset (state);
	g_assert_cmpstr (zif_package_get_summary (package, state, NULL), ==, "GNOME power management service");
	g_ptr_array_unref (array);

	g_object_unref (state);
	g_object_unref (md);
}

static void
zif_md_updateinfo_func (void)
{
	ZifMdUpdateinfo *md;
	GError *error = NULL;
	GPtrArray *array;
	ZifState *state;
	ZifUpdate *update;
	gchar *filename;

	state = zif_state_new ();

	md = zif_md_updateinfo_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_UPDATEINFO);
	filename = zif_test_get_data_file ("fedora/updateinfo.xml.gz");
	zif_md_set_filename (ZIF_MD (md), filename);
	g_free (filename);
	zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	zif_md_set_checksum (ZIF_MD (md), "8dce3986a1841860db16b8b5a3cb603110825252b80a6eb436e5f647e5346955");
	zif_md_set_checksum_uncompressed (ZIF_MD (md), "2ad5aa9d99f475c4950f222696ebf492e6d15844660987e7877a66352098a723");
	array = zif_md_updateinfo_get_detail_for_package (md, "device-mapper-libs;1.02.27-7.fc10;ppc64;fedora", state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));
	g_assert_cmpint (array->len, ==, 1);

	update = g_ptr_array_index (array, 0);
	g_assert_cmpstr (zif_update_get_id (update), ==, "FEDORA-2008-9969");
	g_assert_cmpstr (zif_update_get_title (update), ==, "lvm2-2.02.39-7.fc10");
	g_assert_cmpstr (zif_update_get_description (update), ==, "Fix an incorrect path that prevents the clvmd init script from working and include licence files with the sub-packages.");

	g_ptr_array_unref (array);
	g_object_unref (md);
	g_object_unref (state);
}

static void
zif_md_delta_func (void)
{
	ZifMdDelta *md;
	GError *error = NULL;
	ZifState *state;
	ZifDelta *delta;
	gchar *filename;

	state = zif_state_new ();

	md = zif_md_delta_new ();
	g_assert (md != NULL);
	g_assert (!zif_md_get_is_loaded (ZIF_MD (md)));

	zif_md_set_id (ZIF_MD (md), "fedora");
	zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_UPDATEINFO);
	filename = zif_test_get_data_file ("fedora/prestodelta.xml.gz");
	zif_md_set_filename (ZIF_MD (md), filename);
	g_free (filename);
	zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	zif_md_set_checksum (ZIF_MD (md), "157db37dce190775ff083cb51043e55da6e4abcabfe00584d2a69cc8fd327cae");
	zif_md_set_checksum_uncompressed (ZIF_MD (md), "64b7472f40d355efde22c2156bdebb9c5babe8f35a9f26c6c1ca6b510031d485");

	delta = zif_md_delta_search_for_package (md,
						 "test;0.1-3.fc13;noarch;fedora",
						 "test;0.1-1.fc13;noarch;fedora",
						 state, &error);
	g_assert_no_error (error);
	g_assert (delta != NULL);
	g_assert (zif_md_get_is_loaded (ZIF_MD (md)));
	g_assert_cmpstr (zif_delta_get_filename (delta), ==, "drpms/test-0.1-1.fc13_0.1-3.fc13.i686.drpm");
	g_assert_cmpstr (zif_delta_get_sequence (delta), ==, "test-0.1-1.fc13-9942652a8896b437f4ad8ab930cd32080230");
	g_assert_cmpstr (zif_delta_get_checksum (delta), ==, "000a2b879f9e52e96a6b3c7279b32afbf163cd90ec3887d03aef8aa115f45000");
	g_assert_cmpint (zif_delta_get_size (delta), ==, 81396);

	g_object_unref (delta);
	g_object_unref (md);
	g_object_unref (state);
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
	gboolean ret;
	gchar *filename;
	gchar *pidfile;
	GError *error = NULL;
	GPtrArray *changelog;
	GPtrArray *array;
	ZifConfig *config;
	ZifLock *lock;
	ZifPackage *package;
	ZifRepos *repos;
	ZifState *state;
	ZifStoreLocal *store;
	ZifUpdate *update;

	/* set this up as dummy */
	config = zif_config_new ();
	filename = zif_test_get_data_file ("yum.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	pidfile = g_build_filename (g_get_tmp_dir (), "zif.lock", NULL);
	zif_config_set_local (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	filename = zif_test_get_data_file (".");
	zif_config_set_local (config, "cachedir", filename, NULL);
	g_free (filename);

	state = zif_state_new ();

	lock = zif_lock_new ();
	ret = zif_lock_set_locked (lock, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	store = zif_store_local_new ();
	g_assert (store != NULL);
	filename = zif_test_get_data_file ("root");
	zif_store_local_set_prefix (store, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	repos = zif_repos_new ();
	filename = zif_test_get_data_file ("repos");
	ret = zif_repos_set_repos_dir (repos, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* set a package ID that does not exist */
	package = zif_package_new ();
	ret = zif_package_set_id (package, "hal;2.30.1-1.fc13;i686;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* this is a base class, so this should fail */
	array = zif_package_get_files (package, state, &error);
	g_assert_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED);
	g_assert (array == NULL);
	g_clear_error (&error);

	/* get the update detail */
	zif_state_reset (state);
	update = zif_package_get_update_detail (package, state, &error);
	g_assert_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED);
	g_assert (update == NULL);
	g_object_unref (package);
	g_clear_error (&error);

	/* set a package ID that does exist */
	package = zif_package_new ();
	ret = zif_package_set_id (package, "gnome-power-manager;2.30.1-1.fc13;i686;fedora", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get the update detail */
	zif_state_reset (state);
	update = zif_package_get_update_detail (package, state, &error);
	g_assert_no_error (error);
	g_assert (update != NULL);
	g_assert_cmpstr (zif_update_get_id (update), ==, "FEDORA-2010-9999");

	changelog = zif_update_get_changelog (update);
	g_assert (changelog != NULL);
	g_assert_cmpint (changelog->len, ==, 1);

	/* set to unlocked */
	g_assert (zif_lock_set_unlocked (lock, NULL));

	g_ptr_array_unref (changelog);
	g_object_unref (update);
	g_object_unref (repos);
	g_object_unref (store);
	g_object_unref (lock);
	g_object_unref (package);
	g_object_unref (state);
	g_object_unref (config);
}

static void
zif_package_local_func (void)
{
	ZifPackageLocal *pkg;
	gboolean ret;
	GError *error = NULL;
	gchar *filename;

	pkg = zif_package_local_new ();
	g_assert (pkg != NULL);

	filename = zif_test_get_data_file ("test-0.1-1.fc13.noarch.rpm");
	ret = zif_package_local_set_from_filename (pkg, filename, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);
	g_object_unref (pkg);
}

static void
zif_package_remote_func (void)
{
	ZifPackageRemote *pkg;

	pkg = zif_package_remote_new ();
	g_assert (pkg != NULL);

	g_object_unref (pkg);
}

static void
zif_repos_func (void)
{
	ZifStoreRemote *store;
	ZifConfig *config;
	ZifRepos *repos;
	ZifLock *lock;
	ZifState *state;
	GPtrArray *array;
	GError *error = NULL;
	gboolean ret;
	gchar *filename;
	gchar *pidfile;
	gchar *basearch;

	/* set this up as dummy */
	config = zif_config_new ();
	filename = zif_test_get_data_file ("yum.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	pidfile = g_build_filename (g_get_tmp_dir (), "zif.lock", NULL);
	zif_config_set_local (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	lock = zif_lock_new ();
	g_assert (lock != NULL);
	ret = zif_lock_set_locked (lock, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	repos = zif_repos_new ();
	g_assert (repos != NULL);

	/* use state object */
	state = zif_state_new ();

	filename = zif_test_get_data_file ("repos");
	ret = zif_repos_set_repos_dir (repos, filename, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	array = zif_repos_get_stores (repos, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);
	g_ptr_array_unref (array);

	zif_state_reset (state);
	array = zif_repos_get_stores_enabled (repos, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 2);

	/* check returns error for invalid */
	zif_state_reset (state);
	store = zif_repos_get_store (repos, "does-not-exist", state, &error);
	g_assert (store == NULL);
	g_assert_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED);
	g_clear_error (&error);

	/* get ref for next test */
	store = g_object_ref (g_ptr_array_index (array, 0));
	g_ptr_array_unref (array);
	basearch = zif_config_get_string (config, "basearch", NULL);

	zif_state_reset (state);
	if (g_strcmp0 (basearch, "i386") == 0)
		g_assert_cmpstr (zif_store_remote_get_name (store, state, NULL), ==, "Fedora 13 - i386");
	else
		g_assert_cmpstr (zif_store_remote_get_name (store, state, NULL), ==, "Fedora 13 - x86_64");
	g_object_unref (store);
	g_free (basearch);

	g_object_unref (state);
	g_object_unref (repos);
	g_object_unref (lock);
	g_object_unref (config);
}

static guint _allow_cancel_updates = 0;
static guint _action_updates = 0;
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

static gboolean
zif_state_error_handler_cb (const GError *error, gpointer user_data)
{
	/* emit a warning, this isn't fatal */
	g_debug ("ignoring errors: %s", error->message);
	return TRUE;
}

static void
zif_state_func (void)
{
	ZifState *state;
	ZifState *child;
	gboolean ret;
	guint i;
	GError *error = NULL;

	for (i=0; i<ZIF_STATE_ACTION_UNKNOWN ;i++)
		g_assert (zif_state_action_to_string (i) != NULL);

	state = zif_state_new ();
	g_assert (state != NULL);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "subpercentage-changed", G_CALLBACK (zif_state_test_subpercentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (zif_state_test_allow_cancel_changed_cb), NULL);
	g_signal_connect (state, "action-changed", G_CALLBACK (zif_state_test_action_changed_cb), NULL);

	g_assert (zif_state_get_allow_cancel (state));
	g_assert_cmpint (zif_state_get_action (state), ==, ZIF_STATE_ACTION_UNKNOWN);

	zif_state_set_allow_cancel (state, TRUE);
	g_assert (zif_state_get_allow_cancel (state));

	zif_state_set_allow_cancel (state, FALSE);
	g_assert (!zif_state_get_allow_cancel (state));
	g_assert ((_allow_cancel_updates == 1));

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

	g_assert ((_updates == 1));

	g_assert ((_last_percent == 20));

	ret = zif_state_done (state, NULL);
	ret = zif_state_done (state, NULL);
	ret = zif_state_done (state, NULL);
	g_assert (zif_state_done (state, NULL));

	g_assert (!zif_state_done (state, NULL));
	g_assert ((_updates == 5));
	g_assert ((_last_percent == 100));

	/* ensure allow cancel as we're done */
	g_assert (zif_state_get_allow_cancel (state));

	g_object_unref (state);

	/* reset */
	_updates = 0;
	_allow_cancel_updates = 0;
	_action_updates = 0;
	state = zif_state_new ();
	zif_state_set_allow_cancel (state, TRUE);
	zif_state_set_number_steps (state, 2);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "subpercentage-changed", G_CALLBACK (zif_state_test_subpercentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (zif_state_test_allow_cancel_changed_cb), NULL);
	g_signal_connect (state, "action-changed", G_CALLBACK (zif_state_test_action_changed_cb), NULL);

	// state: |-----------------------|-----------------------|
	// step1: |-----------------------|
	// child:                         |-------------|---------|

	/* PARENT UPDATE */
	ret = zif_state_done (state, NULL);

	g_assert ((_updates == 1));
	g_assert ((_last_percent == 50));

	/* now test with a child */
	child = zif_state_get_child (state);
	zif_state_set_number_steps (child, 2);

	/* set child non-cancellable */
	zif_state_set_allow_cancel (child, FALSE);

	/* ensure both are disallow-cancel */
	g_assert (!zif_state_get_allow_cancel (child));
	g_assert (!zif_state_get_allow_cancel (state));

	/* CHILD UPDATE */
	ret = zif_state_done (child, NULL);

	g_assert ((_updates == 2));
	g_assert ((_last_percent == 75));

	/* child action */
	g_assert (zif_state_action_start (state, ZIF_STATE_ACTION_DOWNLOADING, NULL));

	/* CHILD UPDATE */
	ret = zif_state_done (child, NULL);

	g_assert (!zif_state_action_stop (state));
	g_assert_cmpint (zif_state_get_action (state), ==, ZIF_STATE_ACTION_UNKNOWN);
	g_assert_cmpint (_action_updates, ==, 2);

	g_assert_cmpint (_updates, ==, 3);
	g_assert ((_last_percent == 100));

	/* ensure the child finishing cleared the allow cancel on the parent */
	ret = zif_state_get_allow_cancel (state);
	g_assert (ret);

	/* PARENT UPDATE */
	ret = zif_state_done (state, NULL);

	/* ensure we ignored the duplicate */
	g_assert_cmpint (_updates, ==, 3);
	g_assert ((_last_percent == 100));

	g_object_unref (state);
	g_object_unref (child);

	/* reset */
	_updates = 0;
	state = zif_state_new ();
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
	g_object_unref (child);

	/* test error ignoring */
	state = zif_state_new ();
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
	g_object_unref (child);
	g_clear_error (&error);

	/* test new child gets error handler passed to it */
	state = zif_state_new ();
	error = g_error_new (1, 0, "this is error: %i", 999);
	zif_state_set_error_handler (state, zif_state_error_handler_cb, NULL);
	child = zif_state_get_child (state);
	ret = zif_state_error_handler (child, error);
	g_assert (ret);

	g_object_unref (state);
	g_object_unref (child);
	g_clear_error (&error);

	/* check straight finish */
	state = zif_state_new ();
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
	g_object_unref (child);
}

static void
zif_store_local_func (void)
{
	ZifStoreLocal *store;
	gboolean ret;
	GPtrArray *array;
	ZifPackage *package;
	ZifGroups *groups;
	ZifLock *lock;
	ZifLegal *legal;
	ZifConfig *config;
	ZifState *state;
	GError *error = NULL;
	guint elapsed;
	const gchar *package_id;
	gchar **split;
	const gchar *to_array[] = { NULL, NULL, NULL };
	gchar *filename;
	gchar *pidfile;

	/* set these up as dummy */
	config = zif_config_new ();
	filename = zif_test_get_data_file ("yum.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	pidfile = g_build_filename (g_get_tmp_dir (), "zif.lock", NULL);
	zif_config_set_local (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	filename = zif_test_get_data_file ("licenses.txt");
	legal = zif_legal_new ();
	zif_legal_set_filename (legal, filename);
	g_free (filename);

	/* use state object */
	state = zif_state_new ();

	groups = zif_groups_new ();
	filename = zif_test_get_data_file ("yum-comps-groups.conf");
	ret = zif_groups_set_mapping_file (groups, filename, NULL);
	g_free (filename);
	g_assert (ret);

	store = zif_store_local_new ();
	g_assert (store != NULL);

	lock = zif_lock_new ();
	g_assert (lock != NULL);

	ret = zif_lock_set_locked (lock, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

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

	zif_state_reset (state);
	to_array[0] = "test";
	to_array[1] = NULL;
	g_test_timer_start ();
	array = zif_store_resolve (ZIF_STORE (store), (gchar**)to_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	elapsed = g_test_timer_elapsed ();
	g_assert_cmpint (array->len, ==, 1);
	g_ptr_array_unref (array);
	g_assert_cmpint (elapsed, <, 1000);

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

	zif_state_reset (state);
	to_array[0] = "Test(Interface)";
	array = zif_store_what_provides (ZIF_STORE (store), (gchar**)to_array, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

	/* get this package */
	package = g_ptr_array_index (array, 0);

	package_id = zif_package_get_id (package);
	split = zif_package_id_split (package_id);
	g_assert_cmpstr (split[ZIF_PACKAGE_ID_NAME], ==, "test");
	g_strfreev (split);

	g_assert (g_str_has_suffix (zif_package_get_package_id (package), ";installed"));

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
	g_object_unref (groups);
	g_object_unref (config);
	g_object_unref (lock);
	g_object_unref (legal);
	g_object_unref (state);
}

static void
zif_store_remote_func (void)
{
	ZifGroups *groups;
	ZifStoreRemote *store;
	ZifStoreLocal *store_local;
	GPtrArray *packages;
	ZifConfig *config;
	ZifLock *lock;
	ZifState *state;
	GPtrArray *array;
	gboolean ret;
	GError *error = NULL;
	ZifCategory *category;
	const gchar *in_array[] = { NULL, NULL };
	gchar *filename;
	gchar *pidfile;

	/* set this up as dummy */
	config = zif_config_new ();
	filename = zif_test_get_data_file ("yum.conf");
	zif_config_set_filename (config, filename, NULL);
	g_free (filename);

	pidfile = g_build_filename (g_get_tmp_dir (), "zif.lock", NULL);
	zif_config_set_local (config, "pidfile", pidfile, NULL);
	g_free (pidfile);

	filename = zif_test_get_data_file (".");
	zif_config_set_local (config, "cachedir", filename, NULL);
	g_free (filename);

	/* use state object */
	state = zif_state_new ();

	store = zif_store_remote_new ();
	g_assert (store != NULL);

	lock = zif_lock_new ();
	g_assert (lock != NULL);

	ret = zif_lock_set_locked (lock, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	zif_state_reset (state);
	filename = zif_test_get_data_file ("repos/fedora.repo");
	ret = zif_store_remote_set_from_file (store, filename, "fedora", state, &error);
	g_free (filename);
	g_assert_no_error (error);
	g_assert (ret);

	/* setup state */
	groups = zif_groups_new ();
	filename = zif_test_get_data_file ("yum-comps-groups.conf");
	zif_groups_set_mapping_file (groups, filename, NULL);
	g_free (filename);
	store_local = zif_store_local_new ();
	filename = zif_test_get_data_file ("root");
	zif_store_local_set_prefix (store_local, filename, NULL);
	g_free (filename);

	zif_state_reset (state);
	packages = zif_store_get_packages (ZIF_STORE (store_local), state, &error);
	g_assert_no_error (error);
	g_assert (packages != NULL);
	zif_package_array_filter_newest (packages);
	zif_state_reset (state);
	array = zif_store_get_updates (ZIF_STORE (store), packages, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_ptr_array_unref (array);
	g_ptr_array_unref (packages);

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

	g_ptr_array_unref (array);

	ret = zif_store_remote_set_enabled (store, FALSE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	zif_state_reset (state);
	g_assert (!zif_store_remote_get_enabled (store, state, NULL));

	ret = zif_store_remote_set_enabled (store, TRUE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	zif_state_reset (state);
	g_assert (zif_store_remote_get_enabled (store, state, NULL));

	zif_state_reset (state);
	array = zif_store_get_packages (ZIF_STORE (store), state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 1);

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

	g_object_unref (store);
	g_object_unref (config);
	g_object_unref (lock);
	g_object_unref (state);
	g_object_unref (groups);
	g_object_unref (store_local);
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
	gchar *package_id;
	gboolean ret;
	gchar *evr;
	const gchar *e;
	const gchar *v;
	const gchar *r;
	gchar *filename;
	GError *error = NULL;
	ZifState *state;
	const gchar *package_id_const = "totem;0.1.2;i386;fedora";
	guint i;
	GTimer *timer;
	gchar **split;
	gchar *name;
	const guint iterations = 100000;
	gdouble time_split;
	gdouble time_iter;

	state = zif_state_new ();

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

	filename = zif_file_get_uncompressed_name ("/dave/moo.sqlite.gz");
	g_assert_cmpstr (filename, ==, "/dave/moo.sqlite");
	g_free (filename);

	filename = zif_file_get_uncompressed_name ("/dave/moo.sqlite");
	g_assert_cmpstr (filename, ==, "/dave/moo.sqlite");
	g_free (filename);

	filename = zif_test_get_data_file ("compress.txt.gz");
	ret = zif_file_decompress (filename, "/tmp/comps-fedora.xml", state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);

	filename = zif_test_get_data_file ("compress.txt.bz2");
	ret = zif_file_decompress (filename, "/tmp/moo.sqlite", state, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (filename);

	g_assert_cmpint (zif_time_string_to_seconds (""), ==, 0);
	g_assert_cmpint (zif_time_string_to_seconds ("10"), ==, 0);
	g_assert_cmpint (zif_time_string_to_seconds ("10f"), ==, 0);
	g_assert_cmpint (zif_time_string_to_seconds ("10s"), ==, 10);
	g_assert_cmpint (zif_time_string_to_seconds ("10m"), ==, 600);
	g_assert_cmpint (zif_time_string_to_seconds ("10h"), ==, 36000);
	g_assert_cmpint (zif_time_string_to_seconds ("10d"), ==, 864000);

	/* get the time it takes to split a million strings */
	timer = g_timer_new ();
	for (i=0; i<iterations; i++) {
		split = zif_package_id_split (package_id_const);
		g_strfreev (split);
	}
	time_split = g_timer_elapsed (timer, NULL);

	/* get the time it takes to iterate a million strings */
	g_timer_reset (timer);
	for (i=0; i<iterations; i++) {
		name = zif_package_id_get_name (package_id_const);
		g_free (name);
	}
	time_iter = g_timer_elapsed (timer, NULL);

	/* ensure iter is faster by at least 5 times */
	g_assert_cmpfloat (time_iter * 5, <, time_split);

	g_timer_destroy (timer);
	g_object_unref (state);
}

int
main (int argc, char **argv)
{
	g_type_init ();
	g_thread_init (NULL);
	g_test_init (&argc, &argv, NULL);

	zif_init ();

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/zif/changeset", zif_changeset_func);
	g_test_add_func ("/zif/config", zif_config_func);
	g_test_add_func ("/zif/depend", zif_depend_func);
if (0)	g_test_add_func ("/zif/download", zif_download_func);
	g_test_add_func ("/zif/groups", zif_groups_func);
	g_test_add_func ("/zif/legal", zif_legal_func);
	g_test_add_func ("/zif/lock", zif_lock_func);
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
	g_test_add_func ("/zif/package", zif_package_func);
	g_test_add_func ("/zif/repos", zif_repos_func);
	g_test_add_func ("/zif/state", zif_state_func);
	g_test_add_func ("/zif/store-local", zif_store_local_func);
	g_test_add_func ("/zif/store-remote", zif_store_remote_func);
	g_test_add_func ("/zif/string", zif_string_func);
	g_test_add_func ("/zif/update-info", zif_update_info_func);
	g_test_add_func ("/zif/update", zif_update_func);
	g_test_add_func ("/zif/utils", zif_utils_func);

	return g_test_run ();
}

