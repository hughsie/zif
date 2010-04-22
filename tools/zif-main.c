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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <zif.h>
#include <unistd.h>
#include <sys/types.h>

#include "egg-debug.h"

#include "pk-progress-bar.h"

#define ZIF_MAIN_LOCKING_RETRIES	10
#define ZIF_MAIN_LOCKING_DELAY		2 /* seconds */

static PkProgressBar *progressbar;

/**
 * zif_print_package:
 **/
static void
zif_print_package (ZifPackage *package)
{
	const gchar *package_id;
	const gchar *summary;
	ZifCompletion *completion_tmp;
	gchar **split;

	package_id = zif_package_get_id (package);
	split = zif_package_id_split (package_id);
	completion_tmp = zif_completion_new ();
	summary = zif_package_get_summary (package, NULL, completion_tmp, NULL);
	g_print ("%s-%s.%s (%s)\t%s\n",
		 split[ZIF_PACKAGE_ID_NAME],
		 split[ZIF_PACKAGE_ID_VERSION],
		 split[ZIF_PACKAGE_ID_ARCH],
		 split[ZIF_PACKAGE_ID_DATA],
		 summary);
	g_strfreev (split);
	g_object_unref (completion_tmp);
}

/**
 * zif_print_packages:
 **/
static void
zif_print_packages (GPtrArray *array)
{
	guint i;
	ZifPackage *package;

	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);
		zif_print_package (package);
	}
}

/**
 * zif_completion_percentage_changed_cb:
 **/
static void
zif_completion_percentage_changed_cb (ZifCompletion *completion, guint percentage, gpointer data)
{
	pk_progress_bar_set_value (progressbar, percentage);
	pk_progress_bar_set_percentage (progressbar, percentage);
}

/**
 * zif_completion_subpercentage_changed_cb:
 **/
static void
zif_completion_subpercentage_changed_cb (ZifCompletion *completion, guint percentage, gpointer data)
{
//	pk_progress_bar_set_percentage (progressbar, percentage);
}

/**
 * zif_cmd_download:
 **/
static gboolean
zif_cmd_download (const gchar *package_name, ZifCompletion *completion)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifCompletion *completion_local;
	GPtrArray *store_array;
	const gchar *to_array[] = { NULL, NULL };

	/* setup completion */
	zif_completion_set_number_steps (completion, 3);

	/* add remote stores */
	store_array = zif_store_array_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* resolve package name */
	completion_local = zif_completion_get_child (completion);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar **)to_array, NULL, NULL, NULL, completion_local, &error);
	if (array == NULL) {
		g_print ("failed to get results: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
	if (array->len == 0) {
		g_print ("no package found\n");
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* download package file */
	package = g_ptr_array_index (array, 0);
	ret = zif_package_download (package, "/tmp", NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to download: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

out:
	if (array != NULL) {
		g_ptr_array_unref (array);
	}
	g_object_unref (completion_local);
	g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_get_depends:
 **/
static gboolean
zif_cmd_get_depends (const gchar *package_name, ZifCompletion *completion)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifCompletion *completion_local;
	ZifCompletion *completion_loop;
	GPtrArray *store_array;
	GPtrArray *requires = NULL;
	const ZifDepend *require;
	gchar *require_str;
	GPtrArray *provides;
	const gchar *package_id;
	guint i, j;
	gchar **split;
	const gchar *to_array[] = { NULL, NULL };

	/* setup completion */
	zif_completion_set_number_steps (completion, 3);

	/* add all stores */
	store_array = zif_store_array_new ();
#if 0
	ret = zif_store_array_add_remote_enabled (store_array, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
#endif
	completion_local = zif_completion_get_child (completion);
	ret = zif_store_array_add_local (store_array, NULL, completion, &error);
	if (!ret) {
		g_print ("failed to add local store: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* resolve package name */
	completion_local = zif_completion_get_child (completion);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
	if (array == NULL) {
		g_print ("failed to get results: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
	if (array->len == 0) {
		g_print ("no package found\n");
		goto out;
	}
	package = g_ptr_array_index (array, 0);

	/* this section done */
	zif_completion_done (completion);

	/* get requires */
	completion_local = zif_completion_get_child (completion);
	requires = zif_package_get_requires (package, NULL, completion_local, &error);
	if (requires == NULL) {
		g_print ("failed to get requires: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* match a package to each require */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, requires->len);
	for (i=0; i<requires->len; i++) {

		/* setup deeper completion */
		completion_loop = zif_completion_get_child (completion_local);

		require = g_ptr_array_index (requires, i);
		require_str = zif_depend_to_string (require);
		g_print ("  dependency: %s\n", require_str);
		g_free (require_str);

		/* find the package providing the depend */
		to_array[0] = require->name;
		provides = zif_store_array_what_provides (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_loop, &error);
		if (provides == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* print all of them */
		for (j=0;j<provides->len;j++) {
			package = g_ptr_array_index (provides, j);
			package_id = zif_package_get_id (package);
			split = zif_package_id_split (package_id);
			g_print ("   provider: %s-%s.%s (%s)\n", split[ZIF_PACKAGE_ID_NAME], split[ZIF_PACKAGE_ID_VERSION], split[ZIF_PACKAGE_ID_ARCH], split[ZIF_PACKAGE_ID_DATA]);
			g_strfreev (split);
		}
		g_ptr_array_unref (provides);

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	if (requires != NULL)
		g_ptr_array_unref (requires);
	if (array != NULL)
		g_ptr_array_unref (array);
	g_object_unref (completion_local);
	g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_install:
 **/
static gboolean
zif_cmd_install (const gchar *package_name, ZifCompletion *completion)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifCompletion *completion_local;
	GPtrArray *store_array;
	const gchar *to_array[] = { NULL, NULL };

	/* setup completion */
	zif_completion_set_number_steps (completion, 3);

	/* add all stores */
	store_array = zif_store_array_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add local store: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* check not already installed */
	completion_local = zif_completion_get_child (completion);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
	if (array == NULL) {
		g_print ("failed to get results: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
	if (array->len > 0) {
		g_print ("package already installed\n");
		goto out;
	}
	if (array != NULL) {
		g_ptr_array_unref (array);
	}
	array = NULL;
	g_ptr_array_unref (store_array);

	/* this section done */
	zif_completion_done (completion);

	/* check available */
	store_array = zif_store_array_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* check we can find a package of this name */
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
	if (array == NULL) {
		g_print ("failed to get results: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
	if (array->len == 0) {
		g_print ("could not find package in remote source\n");
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* install this package, TODO: what if > 1? */
	package = g_ptr_array_index (array, 0);
out:
	if (array != NULL) {
		g_ptr_array_unref (array);
	}
	g_ptr_array_unref (store_array);
	g_object_unref (completion_local);
	return ret;
}

/**
 * zif_cmd_refresh_cache:
 **/
static gboolean
zif_cmd_refresh_cache (ZifCompletion *completion, gboolean force)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *store_array;
	ZifCompletion *completion_local;

	/* setup completion */
	zif_completion_set_number_steps (completion, 2);

	/* add remote stores */
	store_array = zif_store_array_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* refresh all ZifRemoteStores */
	completion_local = zif_completion_get_child (completion);
	ret = zif_store_array_refresh (store_array, force, NULL, NULL, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to refresh cache: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);
out:
	g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_update:
 **/
static gboolean
zif_cmd_update (const gchar *package_name, ZifCompletion *completion)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifCompletion *completion_local;
	GPtrArray *store_array;
	const gchar *to_array[] = { NULL, NULL };

	/* setup completion */
	zif_completion_set_number_steps (completion, 4);

	/* add all stores */
	store_array = zif_store_array_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add local store: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* check not already installed */
	completion_local = zif_completion_get_child (completion);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
	if (array == NULL) {
		g_print ("failed to get results: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
	if (array->len == 0) {
		g_print ("package not already installed\n");
		goto out;
	}
	if (array != NULL) {
		g_ptr_array_unref (array);
	}
	array = NULL;
	g_ptr_array_unref (store_array);

	/* this section done */
	zif_completion_done (completion);

	/* check available */
	store_array = zif_store_array_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* check we can find a package of this name */
	completion_local = zif_completion_get_child (completion);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
	if (array == NULL) {
		g_print ("failed to get results: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
	if (array->len == 0) {
		g_print ("could not find package in remote source\n");
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* update this package, TODO: check for newer? */
	package = g_ptr_array_index (array, 0);
out:
	if (array != NULL) {
		g_ptr_array_unref (array);
	}
	g_ptr_array_unref (store_array);
	g_object_unref (completion_local);
	return ret;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GPtrArray *array;
	gboolean ret;
	gboolean offline = FALSE;
//	const gchar *id;
	ZifRepos *repos = NULL;
	GPtrArray *store_array = NULL;
	ZifDownload *download = NULL;
	ZifConfig *config = NULL;
	GPtrArray *packages;
	ZifStoreLocal *store_local = NULL;
	ZifStoreRemote *store_remote = NULL;
	ZifGroups *groups = NULL;
	ZifCompletion *completion = NULL;
	ZifCompletion *completion_local = NULL;
	ZifLock *lock = NULL;
	guint i;
	guint pid;
	guint uid;
	GError *error = NULL;
	ZifPackage *package;
	const gchar *mode;
	const gchar *value;
	GOptionContext *context;
	gchar *options_help;
	gboolean verbose = FALSE;
	gchar *config_file = NULL;
	gchar *http_proxy = NULL;
	gchar *repos_dir = NULL;
	gchar **split;
	const gchar *to_array[] = { NULL, NULL };

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "offline", 'o', 0, G_OPTION_ARG_NONE, &offline,
			_("Work offline when possible"), NULL },
		{ "config", 'c', 0, G_OPTION_ARG_STRING, &config_file,
			_("Use different config file"), NULL },
		{ "proxy", 'p', 0, G_OPTION_ARG_STRING, &http_proxy,
			_("Proxy server setting"), NULL },
		{ NULL}
	};

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();
	zif_init ();

	context = g_option_context_new ("ZIF Console Program");
	g_option_context_set_summary (context, 
		/* new */
		"  clean          Remove cached data\n"
		"  download       Download a package\n"
		"  findpackage    Find a given package given the ID\n"
		"  getcategories  Returns the list of categories\n"
		"  getdepends     List a package's dependencies\n"
		"  getdetails     Display details about a package or group of packages\n"
		"  getfiles       List the files in a package\n"
		"  getgroups      Get the groups the system supports\n"
		"  getpackages    List all packages\n"
		"  getupdates     Check for available package updates\n"
		"  help           Display a helpful usage message\n"
		"  refreshcache   Generate the metadata cache\n"
		"  repolist       Display the configured software repositories\n"
		"  resolve        Find a given package name\n"
		"  searchcategory Search package details for the given category\n"
		"  searchdetails  Search package details for the given string\n"
		"  searchfile     Search packages for the given filename\n"
		"  searchgroup    Search packages in the given group\n"
		"  searchname     Search package name for the given string\n"
		"  whatprovides   Find what package provides the given value\n"
		/* backwards compat */
		"\nThe following commands are provided for backwards compatibility.\n"
		"  check-update   Alias to getupdates\n"
		"  deplist        Alias to getdepends\n"
		"  info           Alias to getdetails\n"
		"  list           Alias to getpackages\n"
		"  provides       Alias to whatprovides\n"
		"  resolvedep     Alias to whatprovides\n"
		"  search         Alias to searchdetails\n"
		"  makecache      Alias to refreshcache\n"
		/* not even started yet */
		"\nThese won't work just yet...\n"
		"  erase          Remove a package or packages from your system\n"
		"  install        Install a package or packages on your system\n"
		"  localinstall   Install a local RPM\n"
		"  reinstall      Reinstall a package\n"
		"  update         Update a package or packages on your system\n"
		"  upgrade        Alias to update\n" /* backwards */
		);

	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	progressbar = pk_progress_bar_new ();

	/* verbose? */
	egg_debug_init (verbose);

	/* fallback */
	if (config_file == NULL)
		config_file = g_strdup ("/etc/yum.conf");

	/* ZifConfig */
	config = zif_config_new ();
	ret = zif_config_set_filename (config, config_file, &error);
	if (!ret) {
		egg_error ("failed to set config: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* are we allowed to access the repos */
	if (!offline)
		zif_config_set_local (config, "network", "1", NULL);

	/* are we root? */
	uid = getuid ();
	if (uid != 0) {
		g_print ("This program has to be run as the root user.\n");
		goto out;
	}

	/* ZifLock */
	lock = zif_lock_new ();
	for (i=0; i<ZIF_MAIN_LOCKING_RETRIES; i++) {
		ret = zif_lock_set_locked (lock, &pid, &error);
		if (ret)
			break;
		g_print ("Failed to lock on try %i of %i, already locked by PID %i (sleeping for %i seconds)\n",
			 i+1, ZIF_MAIN_LOCKING_RETRIES, pid, ZIF_MAIN_LOCKING_DELAY);
		egg_debug ("failed to lock: %s", error->message);
		g_clear_error (&error);
		g_usleep (ZIF_MAIN_LOCKING_DELAY * G_USEC_PER_SEC);
	}

	/* could not lock, even after retrying */
	if (!ret)
		goto out;

	/* ZifDownload */
	download = zif_download_new ();
	ret = zif_download_set_proxy (download, http_proxy, &error);
	if (!ret) {
		egg_error ("failed to set proxy: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ZifStoreLocal */
	store_local = zif_store_local_new ();
	ret = zif_store_local_set_prefix (store_local, "/", &error);
	if (!ret) {
		egg_error ("failed to set prefix: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ZifRepos */
	repos = zif_repos_new ();
	repos_dir = zif_config_get_string (config, "reposdir", &error);
	if (repos_dir == NULL) {
		egg_error ("failed to get repos dir: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_repos_set_repos_dir (repos, repos_dir, &error);
	if (!ret) {
		egg_error ("failed to set repos dir: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ZifGroups */
	groups = zif_groups_new ();
	ret = zif_groups_set_mapping_file (groups, "/usr/share/PackageKit/helpers/yum/yum-comps-groups.conf", &error);
	if (!ret) {
		egg_error ("failed to set mapping file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ZifCompletion */
	completion = zif_completion_new ();
	g_signal_connect (completion, "percentage-changed", G_CALLBACK (zif_completion_percentage_changed_cb), NULL);
	g_signal_connect (completion, "subpercentage-changed", G_CALLBACK (zif_completion_subpercentage_changed_cb), NULL);

	if (argc < 2) {
		g_print ("%s", options_help);
		goto out;
	}

	/* setup progressbar */
	pk_progress_bar_set_padding (progressbar, 30);
	pk_progress_bar_set_size (progressbar, 30);

	mode = argv[1];
	value = argv[2];
	if (g_strcmp0 (mode, "getupdates") == 0 || g_strcmp0 (mode, "check-update") == 0) {

		pk_progress_bar_start (progressbar, "Getting updates");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 5);

		/* get the installed packages */
		completion_local = zif_completion_get_child (completion);
		packages = zif_store_get_packages (ZIF_STORE (store_local), NULL, completion_local, &error);
		if (packages == NULL) {
			g_print ("failed to get local store: %s", error->message);
			g_error_free (error);
			goto out;
		}
		egg_debug ("searching with %i packages", packages->len);

		/* this section done */
		zif_completion_done (completion);

		/* remove any packages that are not newest (think kernel) */
		zif_package_array_filter_newest (packages);

		/* this section done */
		zif_completion_done (completion);

		/* get a store_array of remote stores */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* get updates */
		completion_local = zif_completion_get_child (completion);
		array = zif_store_array_get_updates (store_array, packages, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get updates: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* get update details */
		completion_local = zif_completion_get_child (completion);
		zif_completion_set_number_steps (completion_local, array->len);
		for (i=0; i<array->len; i++) {
			ZifUpdate *update;
			ZifUpdateInfo *info;
			ZifChangeset *changeset;
			GPtrArray *update_infos;
			GPtrArray *changelog;
			ZifCompletion *completion_loop;
			guint j;

			package = g_ptr_array_index (array, i);
			completion_loop = zif_completion_get_child (completion_local);
			update = zif_package_get_update_detail (package, NULL, completion_loop, &error);
			if (update == NULL) {
				g_print ("failed to get update detail for %s: %s\n",
					 zif_package_get_id (package), error->message);
				g_clear_error (&error);

				/* non-fatal */
				zif_completion_finished (completion_loop);
				zif_completion_done (completion_local);
				continue;
			}
			g_print ("\t%s\t%s\n", "kind", zif_update_state_to_string (zif_update_get_kind (update)));
			g_print ("\t%s\t%s\n", "id", zif_update_get_id (update));
			g_print ("\t%s\t%s\n", "title", zif_update_get_title (update));
			g_print ("\t%s\t%s\n", "description", zif_update_get_description (update));
			g_print ("\t%s\t%s\n", "issued", zif_update_get_issued (update));
			update_infos = zif_update_get_update_infos (update);
			for (j=0; j<update_infos->len; j++) {
				info = g_ptr_array_index (update_infos, j);
				g_print ("\tupdateinfo[%i]:kind\t%s\n", j,
					 zif_update_info_kind_to_string (zif_update_info_get_kind (info)));
				g_print ("\tupdateinfo[%i]:title\t%s\n", j,
					 zif_update_info_get_title (info));
				g_print ("\tupdateinfo[%i]:url\t%s\n", j,
					 zif_update_info_get_url (info));
			}
			changelog = zif_update_get_changelog (update);
			for (j=0; j<changelog->len; j++) {
				changeset = g_ptr_array_index (changelog, j);
				g_print ("\tchangelog[%i]:author\t%s\n", j,
					 zif_changeset_get_author (changeset));
				g_print ("\tchangelog[%i]:version\t%s\n", j,
					 zif_changeset_get_version (changeset));
				g_print ("\tchangelog[%i]:description\t%s\n", j,
					 zif_changeset_get_description (changeset));
			}

			/* this section done */
			zif_completion_done (completion_local);
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		g_ptr_array_unref (packages);

		goto out;
	}
	if (g_strcmp0 (mode, "getcategories") == 0) {
		ZifCategory *obj;

		pk_progress_bar_start (progressbar, "Getting categories");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 2);

		/* get a store_array of remote stores */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* get categories */
		completion_local = zif_completion_get_child (completion);
		array = zif_store_array_get_categories (store_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get categories: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		/* dump to console */
		for (i=0; i<array->len; i++) {
			gchar *parent_id;
			gchar *cat_id;
			gchar *name;
			gchar *summary;
			obj = g_ptr_array_index (array, i);
			g_object_get (obj,
				      "parent-id", &parent_id,
				      "cat-id", &cat_id,
				      "name", &name,
				      "summary", &summary,
				      NULL);
			g_print ("parent_id='%s', cat_id='%s', name='%s', summary='%s'\n",
				 parent_id, cat_id, name, summary);
			g_free (parent_id);
			g_free (cat_id);
			g_free (name);
			g_free (summary);
		}
		g_ptr_array_unref (array);
		goto out;
	}

	if (g_strcmp0 (mode, "getgroups") == 0) {
		const gchar *text;

		/* get bitfield */
		array = zif_groups_get_groups (groups, &error);
		if (array == NULL) {
			g_print ("failed to get groups: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* convert to text */
		for (i=0; i<array->len; i++) {
			text = g_ptr_array_index (array, i);
			g_print ("%s\n", text);
		}
		g_ptr_array_unref (array);
		goto out;
	}

	if (g_strcmp0 (mode, "clean") == 0) {

		pk_progress_bar_start (progressbar, "Cleaning");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 2);

		/* get a store_array of remote stores */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* clean all the store_array */
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_clean (store_array, NULL, NULL, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to clean: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		goto out;
	}
	if (g_strcmp0 (mode, "getdepends") == 0 || g_strcmp0 (mode, "deplist") == 0) {

		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Getting depends");
		zif_cmd_get_depends (value, completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);
		goto out;
	}
	if (g_strcmp0 (mode, "download") == 0) {
		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Downloading");
		zif_cmd_download (value, completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);
		goto out;
	}
	if (g_strcmp0 (mode, "erase") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "getfiles") == 0) {
		GPtrArray *files;

		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Get file data");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* resolve */
		completion_local = zif_completion_get_child (completion);
		to_array[0] = value;
		array = zif_store_array_resolve (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* at least one result */
		if (array->len > 0) {
			package = g_ptr_array_index (array, 0);
			completion_local = zif_completion_get_child (completion);
			files = zif_package_get_files (package, NULL, completion_local, &error);
			if (files == NULL) {
				g_print ("failed to get files: %s\n", error->message);
				g_error_free (error);
				goto out;
			}
			for (i=0; i<files->len; i++)
				g_print ("%s\n", (const gchar *) g_ptr_array_index (files, i));
			g_ptr_array_unref (files);
		} else {
			g_print ("Failed to match any packages to '%s'\n", value);
		}

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		/* free results */
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "groupinfo") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "groupinstall") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "grouplist") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "groupremove") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "help") == 0) {
		g_print ("%s", options_help);
		goto out;
	}
	if (g_strcmp0 (mode, "getdetails") == 0 || g_strcmp0 (mode, "info") == 0) {
		const gchar *package_id;
		const gchar *summary;
		const gchar *description;
		const gchar *license;
		const gchar *url;
		guint64 size;

		pk_progress_bar_start (progressbar, "Getting details");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		completion_local = zif_completion_get_child (completion);
		to_array[0] = value;
		array = zif_store_array_resolve (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);

		/* this section done */
		zif_completion_done (completion);

		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		if (array->len == 0) {
			g_print ("no package found\n");
			goto out;
		}
		package = g_ptr_array_index (array, 0);

		package_id = zif_package_get_id (package);
		split = zif_package_id_split (package_id);
		completion_local = zif_completion_get_child (completion);
		summary = zif_package_get_summary (package, NULL, completion_local, NULL);
		description = zif_package_get_description (package, NULL, completion_local, NULL);
		license = zif_package_get_license (package, NULL, completion_local, NULL);
		url = zif_package_get_url (package, NULL, completion_local, NULL);
		size = zif_package_get_size (package, NULL, completion_local, NULL);

		g_print ("Name\t : %s\n", split[ZIF_PACKAGE_ID_NAME]);
		g_print ("Version\t : %s\n", split[ZIF_PACKAGE_ID_VERSION]);
		g_print ("Arch\t : %s\n", split[ZIF_PACKAGE_ID_ARCH]);
		g_print ("Size\t : %" G_GUINT64_FORMAT " bytes\n", size);
		g_print ("Repo\t : %s\n", split[ZIF_PACKAGE_ID_DATA]);
		g_print ("Summary\t : %s\n", summary);
		g_print ("URL\t : %s\n", url);
		g_print ("License\t : %s\n", license);
		g_print ("Description\t : %s\n", description);

		g_strfreev (split);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "install") == 0) {
		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Installing");
		zif_cmd_install (value, completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "list") == 0 || g_strcmp0 (mode, "getpackages") == 0) {

		pk_progress_bar_start (progressbar, "Getting packages");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		array = zif_store_array_get_packages (store_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "localinstall") == 0) {
		if (value == NULL) {
			g_print ("specify a filename");
			value = "/home/hughsie/rpmbuild/REPOS/fedora/11/i386/dum-0.0.1-0.8.20090511git.fc11.i586.rpm";
			//goto out;
		}
		pk_progress_bar_start (progressbar, "Installing");
		package = ZIF_PACKAGE (zif_package_local_new ());
		ret = zif_package_local_set_from_filename (ZIF_PACKAGE_LOCAL (package), value, &error);
		if (!ret)
			egg_error ("failed: %s", error->message);
		zif_package_print (package);
		g_object_unref (package);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "makecache") == 0 || g_strcmp0 (mode, "refreshcache") == 0) {

		pk_progress_bar_start (progressbar, "Refreshing cache");
		zif_cmd_refresh_cache (completion, FALSE);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);
		goto out;
	}
	if (g_strcmp0 (mode, "reinstall") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "repolist") == 0) {
		pk_progress_bar_start (progressbar, "Getting repo list");

		/* get list */
		array = zif_repos_get_stores (repos, NULL, completion, &error);
		if (array == NULL) {
			g_print ("failed to get list of repos: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		/* print */
		for (i=0; i<array->len; i++) {
			store_remote = g_ptr_array_index (array, i);
			g_print ("%s\t\t%s\t\t%s\n",
				 zif_store_get_id (ZIF_STORE (store_remote)),
				 zif_store_remote_get_enabled (store_remote, NULL, completion, NULL) ? "enabled" : "disabled",
				 zif_store_remote_get_name (store_remote, NULL, completion, NULL));
		}

		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "resolve") == 0) {
		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Resolving");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		to_array[0] = value;
		array = zif_store_array_resolve (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "findpackage") == 0) {
		if (value == NULL) {
			g_print ("specify a package_id\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Resolving ID");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* get id */
		if (!zif_package_id_check (value)) {
			g_print ("failed to parse ID: %s\n", value);
			goto out;
		}

		/* find package id */
		completion_local = zif_completion_get_child (completion);
		package = zif_store_array_find_package (store_array, value, NULL, completion_local, &error);
		if (package == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_package (package);
		g_object_unref (package);
		goto out;
	}
	if (g_strcmp0 (mode, "searchname") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Searching name");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		to_array[0] = value;
		array = zif_store_array_search_name (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		g_print ("\n");

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "searchdetails") == 0 || g_strcmp0 (mode, "search") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Searching details");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add local packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* add remote packages */
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		to_array[0] = value;
		array = zif_store_array_search_details (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "searchfile") == 0) {
		if (value == NULL) {
			g_print ("specify a filename\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Searching file");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add local packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add local: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* add remote packages */
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		to_array[0] = value;
		array = zif_store_array_search_file (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "searchgroup") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Search group");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		to_array[0] = value;
		array = zif_store_array_search_group (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "searchcategory") == 0) {
		if (value == NULL) {
			g_print ("specify a category\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Search category");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 2);

		/* add remote stores */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		to_array[0] = value;
		array = zif_store_array_search_category (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "resolvedep") == 0 || g_strcmp0 (mode, "whatprovides") == 0 || g_strcmp0 (mode, "provides") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Provides");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_local (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_store_array_add_remote_enabled (store_array, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		to_array[0] = value;
		array = zif_store_array_what_provides (store_array, (gchar**)to_array, NULL, NULL, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "update") == 0 || g_strcmp0 (mode, "upgrade") == 0) {
		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Updating");
		zif_cmd_update (value, completion);
		pk_progress_bar_end (progressbar);
		g_print ("not yet supported\n");
		goto out;
	}
	
	g_print ("Nothing recognised\n");
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (download != NULL)
		g_object_unref (download);
	if (store_local != NULL)
		g_object_unref (store_local);
	if (repos != NULL)
		g_object_unref (repos);
	if (config != NULL)
		g_object_unref (config);
	if (completion != NULL)
		g_object_unref (completion);
	if (lock != NULL) {
		GError *error_local = NULL;
		ret = zif_lock_set_unlocked (lock, &error_local);
		if (!ret) {
			egg_warning ("failed to unlock: %s", error_local->message);
			g_error_free (error_local);
		}
		g_object_unref (lock);
	}

	g_object_unref (progressbar);
	g_free (repos_dir);
	g_free (http_proxy);
	g_free (config_file);
	g_free (options_help);
	return 0;
}

