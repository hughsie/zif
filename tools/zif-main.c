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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <packagekit-glib/packagekit.h>
#include <zif.h>

#include "egg-debug.h"
#include "egg-string.h"

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
	const PkPackageId *id;
	ZifString *summary;

	id = zif_package_get_id (package);
	summary = zif_package_get_summary (package, NULL);
	g_print ("%s-%s.%s (%s)\t%s\n", id->name, id->version, id->arch, id->data, zif_string_get_value (summary));
	zif_string_unref (summary);
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
}

/**
 * zif_completion_subpercentage_changed_cb:
 **/
static void
zif_completion_subpercentage_changed_cb (ZifCompletion *completion, guint percentage, gpointer data)
{
	pk_progress_bar_set_percentage (progressbar, percentage);
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
	ZifSack *sack;

	/* setup completion */
	zif_completion_set_number_steps (completion, 3);

	/* add remote stores */
	sack = zif_sack_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* resolve package name */
	completion_local = zif_completion_get_child (completion);
	array = zif_sack_resolve (sack, package_name, NULL, completion_local, &error);
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
	g_object_unref (sack);
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
	ZifSack *sack;
	GPtrArray *requires = NULL;
	const ZifDepend *require;
	gchar *require_str;
	GPtrArray *provides;
	const PkPackageId *id;
	guint i, j;
	guint len;

	/* setup completion */
	zif_completion_set_number_steps (completion, 2);

	/* add all stores */
	sack = zif_sack_new ();
#if 0
	ret = zif_sack_add_remote_enabled (sack, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
#endif
	completion_local = zif_completion_get_child (completion);
	ret = zif_sack_add_local (sack, NULL, completion, &error);
	if (!ret) {
		g_print ("failed to add local store: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* resolve package name */
	completion_local = zif_completion_get_child (completion);
	array = zif_sack_resolve (sack, package_name, NULL, completion_local, &error);
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
	requires = zif_package_get_requires (package, &error);
	if (requires == NULL) {
		g_print ("failed to get requires: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* match a package to each require */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, len);
	for (i=0; i<requires->len; i++) {

		/* setup deeper completion */
		completion_loop = zif_completion_get_child (completion_local);

		require = g_ptr_array_index (requires, i);
		require_str = zif_depend_to_string (require);
		g_print ("  dependency: %s\n", require_str);
		g_free (require_str);

		/* find the package providing the depend */
		provides = zif_sack_what_provides (sack, require->name, NULL, completion_loop, &error);
		if (provides == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* print all of them */
		for (j=0;j<provides->len;j++) {
			package = g_ptr_array_index (provides, j);
			id = zif_package_get_id (package);
			g_print ("   provider: %s-%s.%s (%s)\n", id->name, id->version, id->arch, id->data);
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
	if (array != NULL) {
		g_ptr_array_unref (array);
	}
	g_object_unref (completion_local);
	g_object_unref (sack);
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
	ZifSack *sack;

	/* setup completion */
	zif_completion_set_number_steps (completion, 3);

	/* add all stores */
	sack = zif_sack_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_sack_add_local (sack, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add local store: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* check not already installed */
	completion_local = zif_completion_get_child (completion);
	array = zif_sack_resolve (sack, package_name, NULL, completion_local, &error);
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
	g_object_unref (sack);

	/* this section done */
	zif_completion_done (completion);

	/* check available */
	sack = zif_sack_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* check we can find a package of this name */
	array = zif_sack_resolve (sack, package_name, NULL, completion_local, &error);
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
	g_object_unref (sack);
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
	ZifSack *sack;
	ZifCompletion *completion_local;

	/* setup completion */
	zif_completion_set_number_steps (completion, 2);

	/* add remote stores */
	sack = zif_sack_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* refresh all ZifRemoteStores */
	completion_local = zif_completion_get_child (completion);
	ret = zif_sack_refresh (sack, force, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to refresh cache: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);
out:
	g_object_unref (sack);
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
	ZifSack *sack;

	/* setup completion */
	zif_completion_set_number_steps (completion, 4);

	/* add all stores */
	sack = zif_sack_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_sack_add_local (sack, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add local store: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* check not already installed */
	completion_local = zif_completion_get_child (completion);
	array = zif_sack_resolve (sack, package_name, NULL, completion_local, &error);
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
	g_object_unref (sack);

	/* this section done */
	zif_completion_done (completion);

	/* check available */
	sack = zif_sack_new ();
	completion_local = zif_completion_get_child (completion);
	ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* check we can find a package of this name */
	completion_local = zif_completion_get_child (completion);
	array = zif_sack_resolve (sack, package_name, NULL, completion_local, &error);
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
	g_object_unref (sack);
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
//	const gchar *id;
	ZifRepos *repos = NULL;
	ZifSack *sack = NULL;
	ZifDownload *download = NULL;
	ZifConfig *config = NULL;
	ZifStoreLocal *store_local = NULL;
	ZifStoreRemote *store_remote = NULL;
	ZifGroups *groups = NULL;
	ZifCompletion *completion = NULL;
	ZifCompletion *completion_local = NULL;
	ZifLock *lock;
	guint i;
	guint pid;
	GError *error = NULL;
	ZifPackage *package;
	const gchar *mode;
	const gchar *value;
	GOptionContext *context;
	gchar *options_help;
	gboolean verbose = FALSE;
	gboolean profile = FALSE;
	gchar *config_file = NULL;
	gchar *repos_dir = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "profile", 'p', 0, G_OPTION_ARG_NONE, &profile,
			_("Profile ZIF"), NULL },
		{ "config", 'c', 0, G_OPTION_ARG_STRING, &config_file,
			_("Use different config file"), NULL },
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
		/* not even started yet */
		"\nThese won't work just yet...\n"
		"  erase          Remove a package or packages from your system\n"
		"  install        Install a package or packages on your system\n"
		"  localinstall   Install a local RPM\n"
		"  makecache      Alias to refreshcache\n" /* backwards */
		"  refreshcache   Generate the metadata cache\n" /* new */
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
	ret = zif_download_set_proxy (download, NULL, &error);
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

	if (profile) {
		GTimer *timer;
		gdouble time_s;
		gdouble global = 0.0f;
		timer = g_timer_new ();

		/* load local sack */
		g_print ("load sack local... ");
		sack = zif_sack_new ();
		zif_sack_add_local (sack, NULL, completion, NULL);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* resolve local sack */
		g_print ("resolve local sack... ");
		array = zif_sack_resolve (sack, "gnome-power-manager", NULL, NULL, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		if (array->len == 0) {
			g_print ("no package found\n");
			goto out;
		}
		g_ptr_array_unref (array);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* resolve2 local sack */
		g_print ("resolve2 local sack... ");
		array = zif_sack_resolve (sack, "gnome-power-manager", NULL, NULL, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		if (array->len == 0) {
			g_print ("no package found\n");
			goto out;
		}
		g_ptr_array_unref (array);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* searchfile local sack */
		g_print ("searchfile local sack... ");
		array = zif_sack_search_file (sack, "/usr/bin/gnome-power-manager", NULL, NULL, &error);
		if (array == NULL) {
			g_error ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_unref (array);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* whatprovides local sack */
		g_print ("whatprovides local sack... ");
		array = zif_sack_what_provides (sack, "kernel", NULL, NULL, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		if (array->len == 0) {
			g_print ("no package found\n");
			goto out;
		}
		g_ptr_array_unref (array);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* unref local sack */
		g_print ("unref sack local... ");
		g_object_unref (sack);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* load remote sack */
		g_print ("load sack remote... ");
		sack = zif_sack_new ();
		zif_sack_add_remote_enabled (sack, NULL, completion, NULL);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* resolve remote sack */
		g_print ("resolve remote sack... ");
		array = zif_sack_resolve (sack, "gnome-power-manager", NULL, NULL, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_unref (array);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* resolve2 remote sack */
		g_print ("resolve2 remote sack... ");
		array = zif_sack_resolve (sack, "gnome-power-manager", NULL, NULL, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_unref (array);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* searchfile remote sack */
		g_print ("searchfile remote sack... ");
		array = zif_sack_search_file (sack, "/usr/bin/gnome-power-manager", NULL, NULL, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_unref (array);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* whatprovides remote sack */
		g_print ("whatprovides remote sack... ");
		array = zif_sack_what_provides (sack, "kernel", NULL, NULL, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_unref (array);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* unref remote sack */
		g_print ("unref sack remote... ");
		g_object_unref (sack);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* total time */
		g_print ("total time \t : %lf\n", global);
		sack = NULL;
		goto out;
	}

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
		zif_completion_set_number_steps (completion, 2);

		/* get a sack of remote stores */
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* get updates */
		completion_local = zif_completion_get_child (completion);
		array = zif_sack_get_updates (sack, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get updates: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

		zif_print_packages (array);
		g_ptr_array_unref (array);

		goto out;
	}
	if (g_strcmp0 (mode, "getcategories") == 0) {
		const PkCategoryObj *obj;

		pk_progress_bar_start (progressbar, "Getting categories");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 2);

		/* get a sack of remote stores */
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* get categories */
		completion_local = zif_completion_get_child (completion);
		array = zif_sack_get_categories (sack, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get categories: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

		/* dump to console */
		for (i=0; i<array->len; i++) {
			obj = g_ptr_array_index (array, i);
			g_print ("parent_id='%s', cat_id='%s', name='%s', summary='%s'\n",
				 obj->parent_id, obj->cat_id, obj->name, obj->summary);
		}
		g_ptr_array_unref (array);
		goto out;
	}

	if (g_strcmp0 (mode, "getgroups") == 0) {
		PkBitfield group_bitfield;
		gchar *text;

		/* get bitfield */
		group_bitfield = zif_groups_get_groups (groups, &error);
		if (group_bitfield == 0) {
			g_print ("failed to get groups: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* convert to text */
		text = pk_group_bitfield_to_text (group_bitfield);
		g_strdelimit (text, ";", '\n');

		/* print it */
		g_print ("%s\n", text);
		g_free (text);
		goto out;
	}

	if (g_strcmp0 (mode, "clean") == 0) {

		pk_progress_bar_start (progressbar, "Cleaning");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 2);

		/* get a sack of remote stores */
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* clean all the sack */
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_clean (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to clean: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		goto out;
	}
	if (g_strcmp0 (mode, "getdepends") == 0 || g_strcmp0 (mode, "deplist") == 0) {

		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Getting depends");
		zif_cmd_get_depends (value, completion);
		goto out;
	}
	if (g_strcmp0 (mode, "download") == 0) {
		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Downloading");
		zif_cmd_download (value, completion);
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
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* resolve */
		completion_local = zif_completion_get_child (completion);
		array = zif_sack_resolve (sack, value, NULL, completion_local, &error);
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
			files = zif_package_get_files (package, NULL);
			for (i=0; i<files->len; i++)
				g_print ("%s\n", (const gchar *) g_ptr_array_index (files, i));
			g_ptr_array_unref (files);
		} else {
			g_print ("Failed to match any packages to '%s'\n", value);
		}

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
		const PkPackageId *id;
		ZifString *summary;
		ZifString *description;
		ZifString *license;
		ZifString *url;
		guint64 size;

		pk_progress_bar_start (progressbar, "Getting details");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
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
		array = zif_sack_resolve (sack, value, NULL, completion_local, &error);

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

		id = zif_package_get_id (package);
		summary = zif_package_get_summary (package, NULL);
		description = zif_package_get_description (package, NULL);
		license = zif_package_get_license (package, NULL);
		url = zif_package_get_url (package, NULL);
		size = zif_package_get_size (package, NULL);

		g_print ("Name\t : %s\n", id->name);
		g_print ("Version\t : %s\n", id->version);
		g_print ("Arch\t : %s\n", id->arch);
		g_print ("Size\t : %" G_GUINT64_FORMAT " bytes\n", size);
		g_print ("Repo\t : %s\n", id->data);
		g_print ("Summary\t : %s\n", zif_string_get_value (summary));
		g_print ("URL\t : %s\n", zif_string_get_value (url));
		g_print ("License\t : %s\n", zif_string_get_value (license));
		g_print ("Description\t : %s\n", zif_string_get_value (description));

		zif_string_unref (summary);
		zif_string_unref (url);
		zif_string_unref (license);
		zif_string_unref (description);

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
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "list") == 0 || g_strcmp0 (mode, "getpackages") == 0) {

		pk_progress_bar_start (progressbar, "Getting packages");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		array = zif_sack_get_packages (sack, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

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
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "makecache") == 0 || g_strcmp0 (mode, "refreshcache") == 0) {

		pk_progress_bar_start (progressbar, "Refreshing cache");
		zif_cmd_refresh_cache (completion, FALSE);
		g_print ("not yet supported\n");
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
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		array = zif_sack_resolve (sack, value, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "findpackage") == 0) {
		PkPackageId *id;

		if (value == NULL) {
			g_print ("specify a package_id\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Resolving ID");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* get id */
		id = pk_package_id_new_from_string (value);
		if (id == NULL) {
			g_print ("failed to parse ID: %s\n", value);
			goto out;
		}

		/* find package id */
		completion_local = zif_completion_get_child (completion);
		package = zif_sack_find_package (sack, id, NULL, completion_local, &error);
		pk_package_id_free (id);
		if (package == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

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
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		array = zif_sack_search_name (sack, value, NULL, completion_local, &error);
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

		/* add both local and remote packages */
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		array = zif_sack_search_details (sack, value, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

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

		/* add both local and remote packages */
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		array = zif_sack_search_file (sack, value, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

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
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		array = zif_sack_search_group (sack, value, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

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
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		array = zif_sack_search_category (sack, value, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "resolvedep") == 0 || g_strcmp0 (mode, "whatprovides") == 0 || g_strcmp0 (mode, "provides") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Resolving");

		/* setup completion with the correct number of steps */
		zif_completion_set_number_steps (completion, 3);

		/* add both local and remote packages */
		sack = zif_sack_new ();
		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_local (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		ret = zif_sack_add_remote_enabled (sack, NULL, completion_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		completion_local = zif_completion_get_child (completion);
		array = zif_sack_what_provides (sack, value, NULL, completion_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* newline for output */
		g_print ("\n");

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
		g_print ("not yet supported\n");
		goto out;
	}
	
	g_print ("Nothing recognised\n");
out:
	if (sack != NULL)
		g_object_unref (sack);
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
		ret = zif_lock_set_unlocked (lock, &error);
		if (!ret) {
			egg_warning ("failed to unlock: %s", error->message);
			g_error_free (error);
		}
		g_object_unref (lock);
	}

	g_object_unref (progressbar);
	g_free (repos_dir);
	g_free (config_file);
	g_free (options_help);
	return 0;
}

