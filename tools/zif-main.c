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
#include <glib/gi18n.h>
#include <zif.h>
#include <unistd.h>
#include <sys/types.h>
#include <glib/gstdio.h>

#include "zif-progress-bar.h"

#define ZIF_MAIN_LOCKING_RETRIES	10
#define ZIF_MAIN_LOCKING_DELAY		2 /* seconds */

static ZifProgressBar *progressbar;

/**
 * zif_print_package:
 **/
static void
zif_print_package (ZifPackage *package, guint padding)
{
	const gchar *package_id;
	const gchar *summary;
	gchar *padding_str;
	ZifState *state_tmp;
	gchar **split;

	package_id = zif_package_get_id (package);
	split = zif_package_id_split (package_id);
	state_tmp = zif_state_new ();
	summary = zif_package_get_summary (package, state_tmp, NULL);
	if (padding > 0) {
		padding_str = g_strnfill (padding - strlen (package_id), ' ');
	} else {
		padding_str = g_strnfill (2, ' ');
	}
	g_print ("%s-%s.%s (%s)%s%s\n",
		 split[ZIF_PACKAGE_ID_NAME],
		 split[ZIF_PACKAGE_ID_VERSION],
		 split[ZIF_PACKAGE_ID_ARCH],
		 split[ZIF_PACKAGE_ID_DATA],
		 padding_str, summary);
	g_free (padding_str);
	g_strfreev (split);
	g_object_unref (state_tmp);
}

/**
 * zif_print_packages:
 **/
static void
zif_print_packages (GPtrArray *array)
{
	guint i, j;
	guint max = 0;
	const gchar *package_id;
	ZifPackage *package;

	/* get the padding required */
	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);
		package_id = zif_package_get_id (package);
		j = strlen (package_id);
		if (j > max)
			max = j;
	}

	/* print the packages */
	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);
		zif_print_package (package, max + 2);
	}
}

/**
 * zif_state_percentage_changed_cb:
 **/
static void
zif_state_percentage_changed_cb (ZifState *state, guint percentage, gpointer data)
{
	pk_progress_bar_set_value (progressbar, percentage);
	pk_progress_bar_set_percentage (progressbar, percentage);
}

/**
 * zif_state_subpercentage_changed_cb:
 **/
static void
zif_state_subpercentage_changed_cb (ZifState *state, guint percentage, gpointer data)
{
//	pk_progress_bar_set_percentage (progressbar, percentage);
}

/**
 * zif_state_allow_cancel_changed_cb:
 **/
static void
zif_state_allow_cancel_changed_cb (ZifState *state, gboolean allow_cancel, gpointer data)
{
	pk_progress_bar_set_allow_cancel (progressbar, allow_cancel);
}

/**
 * zif_state_action_changed_cb:
 **/
static void
zif_state_action_changed_cb (ZifState *state, ZifStateAction action, const gchar *action_hint, gpointer data)
{
	if (action == ZIF_STATE_ACTION_UNKNOWN)
		return;
	g_print ("\n%s: %s\n", zif_state_action_to_string (action), action_hint != NULL ? action_hint : "");
}

/**
 * zif_cmd_download:
 **/
static gboolean
zif_cmd_download (const gchar *package_name, ZifState *state)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifState *state_local;
	GPtrArray *store_array;
	const gchar *to_array[] = { NULL, NULL };

	/* setup state */
	zif_state_set_number_steps (state, 3);

	/* add remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* resolve package name */
	state_local = zif_state_get_child (state);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar **)to_array, state_local, &error);
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
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* download package file */
	package = g_ptr_array_index (array, 0);
	ret = zif_package_download (package, "/tmp", state_local, &error);
	if (!ret) {
		g_print ("failed to download: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

out:
	if (array != NULL) {
		g_ptr_array_unref (array);
	}
	g_object_unref (state_local);
	g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_get_depends:
 **/
static gboolean
zif_cmd_get_depends (const gchar *package_name, ZifState *state)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;
	GPtrArray *store_array;
	GPtrArray *requires = NULL;
	ZifDepend *require;
	const gchar *require_str;
	GPtrArray *provides;
	const gchar *package_id;
	guint i, j;
	gchar **split;
	const gchar *to_array[] = { NULL, NULL };
	GString *string;

	/* setup progressbar */
	pk_progress_bar_start (progressbar, "Getting depends");

	/* use a temp string to get output results */
	string = g_string_new ("");

	/* setup state */
	zif_state_set_number_steps (state, 4);

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
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_local (store_array, state, &error);
	if (!ret) {
		g_print ("failed to add local store: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* resolve package name */
	state_local = zif_state_get_child (state);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, state_local, &error);
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
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* get requires */
	state_local = zif_state_get_child (state);
	requires = zif_package_get_requires (package, state_local, &error);
	if (requires == NULL) {
		g_print ("failed to get requires: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* match a package to each require */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, requires->len);
	for (i=0; i<requires->len; i++) {

		/* setup deeper state */
		state_loop = zif_state_get_child (state_local);

		require = g_ptr_array_index (requires, i);
		require_str = zif_depend_get_description (require);
		g_string_append_printf (string, "  dependency: %s\n", require_str);

		/* find the package providing the depend */
		to_array[0] = zif_depend_get_name (require);
		provides = zif_store_array_what_provides (store_array, (gchar**)to_array, state_loop, &error);
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
			g_string_append_printf (string, "   provider: %s-%s.%s (%s)\n",
						split[ZIF_PACKAGE_ID_NAME],
						split[ZIF_PACKAGE_ID_VERSION],
						split[ZIF_PACKAGE_ID_ARCH],
						split[ZIF_PACKAGE_ID_DATA]);
			g_strfreev (split);
		}
		g_ptr_array_unref (provides);

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* no more progressbar */
	pk_progress_bar_end (progressbar);

	/* success */
	g_print ("%s", string->str);
out:
	g_string_free (string, TRUE);
	if (requires != NULL)
		g_ptr_array_unref (requires);
	if (array != NULL)
		g_ptr_array_unref (array);
	g_object_unref (state_local);
	g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_install:
 **/
static gboolean
zif_cmd_install (const gchar *package_name, ZifState *state)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifState *state_local;
	GPtrArray *store_array;
	const gchar *to_array[] = { NULL, NULL };

	/* setup state */
	zif_state_set_number_steps (state, 3);

	/* add all stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_local (store_array, state_local, &error);
	if (!ret) {
		g_print ("failed to add local store: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* check not already installed */
	state_local = zif_state_get_child (state);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, state_local, &error);
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
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* check available */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* check we can find a package of this name */
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, state_local, &error);
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
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* install this package, TODO: what if > 1? */
	package = g_ptr_array_index (array, 0);
out:
	if (array != NULL) {
		g_ptr_array_unref (array);
	}
	g_ptr_array_unref (store_array);
	g_object_unref (state_local);
	return ret;
}

/**
 * zif_cmd_refresh_cache:
 **/
static gboolean
zif_cmd_refresh_cache (ZifState *state, gboolean force)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *store_array;
	ZifState *state_local;

	/* setup state */
	zif_state_set_number_steps (state, 2);

	/* add remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* refresh all ZifRemoteStores */
	state_local = zif_state_get_child (state);
	ret = zif_store_array_refresh (store_array, force, state_local, &error);
	if (!ret) {
		g_print ("failed to refresh cache: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;
out:
	g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_update:
 **/
static gboolean
zif_cmd_update (const gchar *package_name, ZifState *state)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifState *state_local;
	GPtrArray *store_array;
	const gchar *to_array[] = { NULL, NULL };

	/* setup state */
	zif_state_set_number_steps (state, 4);

	/* add all stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_local (store_array, state_local, &error);
	if (!ret) {
		g_print ("failed to add local store: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* check not already installed */
	state_local = zif_state_get_child (state);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, state_local, &error);
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
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* check available */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
	if (!ret) {
		g_print ("failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* check we can find a package of this name */
	state_local = zif_state_get_child (state);
	to_array[0] = package_name;
	array = zif_store_array_resolve (store_array, (gchar**)to_array, state_local, &error);
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
	ret = zif_state_done (state, &error);
	if (!ret)
		goto out;

	/* update this package, TODO: check for newer? */
	package = g_ptr_array_index (array, 0);
out:
	if (array != NULL) {
		g_ptr_array_unref (array);
	}
	g_ptr_array_unref (store_array);
	g_object_unref (state_local);
	return ret;
}

static ZifState *_state = NULL;

/**
 * zif_main_sigint_cb:
 **/
static void
zif_main_sigint_cb (int sig)
{
	GCancellable *cancellable;
	g_debug ("Handling SIGINT");

	/* restore default ASAP, as the cancels might hang */
	signal (SIGINT, SIG_DFL);

	/* cancel any tasks still running */
	if (_state != NULL) {
		cancellable = zif_state_get_cancellable (_state);
		g_cancellable_cancel (cancellable);
	}
}

/**
 * zif_strpad:
 **/
static gchar *
zif_strpad (const gchar *data, guint length)
{
	gint size;
	guint data_len;
	gchar *text;
	gchar *padding;

	if (data == NULL)
		return g_strnfill (length, ' ');

	/* ITS4: ignore, only used for formatting */
	data_len = strlen (data);

	/* calculate */
	size = (length - data_len);
	if (size <= 0)
		return g_strdup (data);

	padding = g_strnfill (size, ' ');
	text = g_strdup_printf ("%s%s", data, padding);
	g_free (padding);
	return text;
}

/**
 * zif_log_ignore_cb:
 **/
static void
zif_log_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

/**
 * zif_log_handler_cb:
 **/
static void
zif_log_handler_cb (const gchar *log_domain, GLogLevelFlags log_level,
		    const gchar *message, gpointer user_data)
{
	gchar str_time[255];
	time_t the_time;

	/* header always in green */
	time (&the_time);
	strftime (str_time, 254, "%H:%M:%S", localtime (&the_time));
	g_print ("%c[%dmTI:%s\t", 0x1B, 32, str_time);

	/* critical is also in red */
	if (log_level == G_LOG_LEVEL_CRITICAL ||
	    log_level == G_LOG_LEVEL_WARNING ||
	    log_level == G_LOG_LEVEL_ERROR) {
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 31, message, 0x1B, 0);
	} else {
		/* debug in blue */
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 34, message, 0x1B, 0);
	}
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
	ZifConfig *config = NULL;
	GPtrArray *packages;
	ZifStoreLocal *store_local = NULL;
	ZifStoreRemote *store_remote = NULL;
	ZifGroups *groups = NULL;
	ZifState *state = NULL;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifLock *lock = NULL;
	ZifRelease *release = NULL;
	guint i;
	guint pid;
	guint uid;
	guint age = 0;
	guint version;
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
	gchar *root = NULL;
	gchar **distro_id_split = NULL;
	const gchar *to_array[] = { NULL, NULL };

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "offline", 'o', 0, G_OPTION_ARG_NONE, &offline,
			_("Work offline when possible"), NULL },
		{ "config", 'c', 0, G_OPTION_ARG_STRING, &config_file,
			_("Use different config file"), NULL },
		{ "root", 'c', 0, G_OPTION_ARG_STRING, &root,
			_("Use different rpm database root"), NULL },
		{ "proxy", 'p', 0, G_OPTION_ARG_STRING, &http_proxy,
			_("Proxy server setting"), NULL },
		{ "age", 'a', 0, G_OPTION_ARG_INT, &age,
			_("Permitted age of the cache in seconds, 0 for never (default)"), NULL },
		{ NULL}
	};

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();
	zif_init ();

	context = g_option_context_new ("ZIF Console Program");
	g_option_context_set_summary (context, 
		/* new */
		"  clean           Remove cached data\n"
		"  download        Download a package\n"
		"  find-package    Find a given package given the ID\n"
		"  get-categories  Returns the list of categories\n"
		"  get-depends     List a package's dependencies\n"
		"  get-details     Display details about a package or group of packages\n"
		"  get-files       List the files in a package\n"
		"  get-groups      Get the groups the system supports\n"
		"  get-packages    List all packages\n"
		"  get-updates     Check for available package updates\n"
		"  get-upgrades    Check for newer operating system versions\n"
		"  help            Display a helpful usage message\n"
		"  refresh-cache   Generate the metadata cache\n"
		"  repo-list       Display the configured software repositories\n"
		"  repo-enable     Enable a specific software repository\n"
		"  repo-disable    Disable a specific software repository\n"
		"  resolve         Find a given package name\n"
		"  search-category Search package details for the given category\n"
		"  search-details  Search package details for the given string\n"
		"  search-file     Search packages for the given filename\n"
		"  search-group    Search packages in the given group\n"
		"  search-name     Search package name for the given string\n"
		"  upgrade         Upgrade the operating system to a newer version\n"
		"  what-conflicts  Find what package conflicts with the given value\n"
		"  what-obsoletes  Find what package obsoletes the given value\n"
		"  what-provides   Find what package provides the given value\n"
		);

	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	progressbar = pk_progress_bar_new ();

	/* do stuff on ctrl-c */
	signal (SIGINT, zif_main_sigint_cb);

	/* verbose? */
	if (verbose) {
		g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
		g_log_set_handler ("Zif", G_LOG_LEVEL_ERROR |
					  G_LOG_LEVEL_CRITICAL |
					  G_LOG_LEVEL_DEBUG |
					  G_LOG_LEVEL_WARNING,
				   zif_log_handler_cb, NULL);
	} else {
		/* hide all debugging */
		g_log_set_handler ("Zif", G_LOG_LEVEL_DEBUG,
				   zif_log_ignore_cb, NULL);
	}

	/* fallback */
	if (config_file == NULL)
		config_file = g_strdup ("/etc/yum.conf");
	if (root == NULL)
		root = g_strdup ("/");

	/* ZifConfig */
	config = zif_config_new ();
	ret = zif_config_set_filename (config, config_file, &error);
	if (!ret) {
		g_error ("failed to set config: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_config_set_string (config, "http_proxy", http_proxy, &error);
	if (!ret) {
		g_error ("failed to set proxy: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* are we allowed to access the repos */
	if (!offline)
		zif_config_set_boolean (config, "network", TRUE, NULL);

	/* set the maximum age of the repo data */
	if (age > 0)
		zif_config_set_uint (config, "max-age", age, NULL);

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
		g_debug ("failed to lock: %s", error->message);
		g_clear_error (&error);
		g_usleep (ZIF_MAIN_LOCKING_DELAY * G_USEC_PER_SEC);
	}

	/* could not lock, even after retrying */
	if (!ret)
		goto out;

	/* ZifStoreLocal */
	store_local = zif_store_local_new ();
	ret = zif_store_local_set_prefix (store_local, root, &error);
	if (!ret) {
		g_error ("failed to set prefix: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ZifRepos */
	repos = zif_repos_new ();
	repos_dir = zif_config_get_string (config, "reposdir", &error);
	if (repos_dir == NULL) {
		g_error ("failed to get repos dir: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_repos_set_repos_dir (repos, repos_dir, &error);
	if (!ret) {
		g_error ("failed to set repos dir: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ZifGroups */
	groups = zif_groups_new ();
	ret = zif_groups_set_mapping_file (groups, "/usr/share/PackageKit/helpers/yum/yum-comps-groups.conf", &error);
	if (!ret) {
		g_error ("failed to set mapping file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ZifRelease */
	release = zif_release_new ();
	zif_release_set_boot_dir (release, "/boot/upgrade");
	zif_release_set_cache_dir (release, "/var/cache/PackageKit");
	zif_release_set_repo_dir (release, "/var/cache/yum/preupgrade");
	zif_release_set_uri (release, "http://mirrors.fedoraproject.org/releases.txt");

	/* ZifState */
	state = zif_state_new ();
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_state_percentage_changed_cb), NULL);
	g_signal_connect (state, "subpercentage-changed", G_CALLBACK (zif_state_subpercentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (zif_state_allow_cancel_changed_cb), NULL);
	g_signal_connect (state, "action-changed", G_CALLBACK (zif_state_action_changed_cb), NULL);

	/* for the signal handler */
	_state = state;

	if (argc < 2) {
		g_print ("%s", options_help);
		goto out;
	}

	/* setup progressbar */
	pk_progress_bar_set_padding (progressbar, 30);
	pk_progress_bar_set_size (progressbar, 30);

	mode = argv[1];
	value = argv[2];
	if (g_strcmp0 (mode, "get-updates") == 0) {

		pk_progress_bar_start (progressbar, "Getting updates");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 5);

		/* get the installed packages */
		state_local = zif_state_get_child (state);
		packages = zif_store_get_packages (ZIF_STORE (store_local), state_local, &error);
		if (packages == NULL) {
			g_print ("failed to get local store: %s", error->message);
			g_error_free (error);
			goto out;
		}
		g_debug ("searching with %i packages", packages->len);

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* remove any packages that are not newest (think kernel) */
		zif_package_array_filter_newest (packages);

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* get a store_array of remote stores */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* get updates */
		state_local = zif_state_get_child (state);
		array = zif_store_array_get_updates (store_array, packages, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get updates: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* get update details */
		state_local = zif_state_get_child (state);
		zif_state_set_number_steps (state_local, array->len);
		for (i=0; i<array->len; i++) {
			ZifUpdate *update;
			ZifUpdateInfo *info;
			ZifChangeset *changeset;
			GPtrArray *update_infos;
			GPtrArray *changelog;
			guint j;

			package = g_ptr_array_index (array, i);
			state_loop = zif_state_get_child (state_local);
			update = zif_package_get_update_detail (package, state_loop, &error);
			if (update == NULL) {
				g_print ("failed to get update detail for %s: %s\n",
					 zif_package_get_id (package), error->message);
				g_clear_error (&error);

				/* non-fatal */
				ret = zif_state_finished (state_loop, &error);
				if (!ret)
					goto out;
				ret = zif_state_done (state_local, &error);
				if (!ret)
					goto out;
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
			ret = zif_state_done (state_local, &error);
			if (!ret)
				goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		g_ptr_array_unref (packages);

		goto out;
	}
	if (g_strcmp0 (mode, "get-categories") == 0) {
		ZifCategory *obj;

		pk_progress_bar_start (progressbar, "Getting categories");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 2);

		/* get a store_array of remote stores */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* get categories */
		state_local = zif_state_get_child (state);
		array = zif_store_array_get_categories (store_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get categories: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

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

	if (g_strcmp0 (mode, "get-groups") == 0) {
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

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 2);

		/* get a store_array of remote stores */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* clean all the store_array */
		state_local = zif_state_get_child (state);
		ret = zif_store_array_clean (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to clean: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		goto out;
	}
	if (g_strcmp0 (mode, "get-depends") == 0) {

		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		zif_cmd_get_depends (value, state);
		goto out;
	}
	if (g_strcmp0 (mode, "download") == 0) {
		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Downloading");
		zif_cmd_download (value, state);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);
		goto out;
	}
	if (g_strcmp0 (mode, "erase") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "get-files") == 0) {
		GPtrArray *files;

		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Get file data");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* resolve */
		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_resolve (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* at least one result */
		if (array->len > 0) {
			package = g_ptr_array_index (array, 0);
			state_local = zif_state_get_child (state);
			files = zif_package_get_files (package, state_local, &error);
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
	if (g_strcmp0 (mode, "group-info") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "group-install") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "group-list") == 0) {
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
	if (g_strcmp0 (mode, "get-details") == 0) {
		const gchar *summary;
		const gchar *description;
		const gchar *license;
		const gchar *url;
		guint64 size;

		pk_progress_bar_start (progressbar, "Getting details");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 4);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_resolve (store_array, (gchar**)to_array, state_local, &error);

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		if (array->len == 0) {
			g_print ("no package found\n");
			goto out;
		}

		state_local = zif_state_get_child (state);
		zif_state_set_number_steps (state_local, array->len);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		for (i=0; i<array->len; i++) {
			package = g_ptr_array_index (array, i);

			state_loop = zif_state_get_child (state_local);
			summary = zif_package_get_summary (package, state_loop, NULL);
			description = zif_package_get_description (package, state_loop, NULL);
			license = zif_package_get_license (package, state_loop, NULL);
			url = zif_package_get_url (package, state_loop, NULL);
			size = zif_package_get_size (package, state_loop, NULL);

			g_print ("Name\t : %s\n", zif_package_get_name (package));
			g_print ("Version\t : %s\n", zif_package_get_version (package));
			g_print ("Arch\t : %s\n", zif_package_get_arch (package));
			g_print ("Size\t : %" G_GUINT64_FORMAT " bytes\n", size);
			g_print ("Repo\t : %s\n", zif_package_get_data (package));
			g_print ("Summary\t : %s\n", summary);
			g_print ("URL\t : %s\n", url);
			g_print ("License\t : %s\n", license);
			g_print ("Description\t : %s\n", description);

			/* this section done */
			ret = zif_state_done (state_local, &error);
			if (!ret)
				goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "install") == 0) {
		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Installing");
		zif_cmd_install (value, state);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "get-packages") == 0) {

		pk_progress_bar_start (progressbar, "Getting packages");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		array = zif_store_array_get_packages (store_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "local-install") == 0) {
		if (value == NULL) {
			g_print ("specify a filename");
			value = "/home/hughsie/rpmbuild/REPOS/fedora/11/i386/zif-0.1.0-0.8.20090511git.fc11.i586.rpm";
			//goto out;
		}
		pk_progress_bar_start (progressbar, "Installing");
		package = ZIF_PACKAGE (zif_package_local_new ());
		ret = zif_package_local_set_from_filename (ZIF_PACKAGE_LOCAL (package), value, &error);
		if (!ret)
			g_error ("failed: %s", error->message);
		zif_package_print (package);
		g_object_unref (package);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "refresh-cache") == 0) {

		pk_progress_bar_start (progressbar, "Refreshing cache");
		zif_cmd_refresh_cache (state, FALSE);

		/* no more progressbar */
		pk_progress_bar_end (progressbar);
		goto out;
	}
	if (g_strcmp0 (mode, "reinstall") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "repo-list") == 0) {
		guint max_length = 0;
		guint length;
		gchar *id_pad;

		pk_progress_bar_start (progressbar, "Getting repo list");

		/* get list */
		array = zif_repos_get_stores (repos, state, &error);
		if (array == NULL) {
			g_print ("failed to get list of repos: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		/* get maximum id string length */
		for (i=0; i<array->len; i++) {
			store_remote = g_ptr_array_index (array, i);
			/* ITS4: ignore, only used for formatting */
			length = strlen (zif_store_get_id (ZIF_STORE (store_remote)));
			if (length > max_length)
				max_length = length;
		}

		/* print */
		for (i=0; i<array->len; i++) {
			store_remote = g_ptr_array_index (array, i);
			id_pad = zif_strpad (zif_store_get_id (ZIF_STORE (store_remote)), max_length);
			g_print ("%s\t%s\t%s\n",
				 id_pad,
				 zif_store_remote_get_enabled (store_remote, state, NULL) ? "enabled " : "disabled",
				 zif_store_remote_get_name (store_remote, state, NULL));
			g_free (id_pad);
		}

		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "repo-enable") == 0) {
		if (value == NULL) {
			g_print ("specify a repo name\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Enabling repo");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 2);

		/* get repo */
		state_local = zif_state_get_child (state);
		store_remote = zif_repos_get_store (repos, value, state_local, &error);
		if (store_remote == NULL) {
			g_print ("failed to find repo: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* change the enabled state */
		ret = zif_store_remote_set_enabled (store_remote, TRUE, &error);
		if (!ret) {
			g_print ("failed to change repo state: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		goto out;
	}
	if (g_strcmp0 (mode, "repo-disable") == 0) {
		if (value == NULL) {
			g_print ("specify a repo name\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Disabling repo");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 2);

		/* get repo */
		state_local = zif_state_get_child (state);
		store_remote = zif_repos_get_store (repos, value, state_local, &error);
		if (store_remote == NULL) {
			g_print ("failed to find repo: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* change the enabled state */
		ret = zif_store_remote_set_enabled (store_remote, FALSE, &error);
		if (!ret) {
			g_print ("failed to change repo state: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		goto out;
	}
	if (g_strcmp0 (mode, "resolve") == 0) {
		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Resolving");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_resolve (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "find-package") == 0) {
		if (value == NULL) {
			g_print ("specify a package_id\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Resolving ID");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* get id */
		if (!zif_package_id_check (value)) {
			g_print ("failed to parse ID: %s\n", value);
			goto out;
		}

		/* find package id */
		state_local = zif_state_get_child (state);
		package = zif_store_array_find_package (store_array, value, state_local, &error);
		if (package == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_package (package, 0);
		g_object_unref (package);
		goto out;
	}
	if (g_strcmp0 (mode, "search-name") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Searching name");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_search_name (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		g_print ("\n");

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "search-details") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Searching details");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add local packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* add remote packages */
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_search_details (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "search-file") == 0) {
		if (value == NULL) {
			g_print ("specify a filename\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Searching file");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add local packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add local: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* add remote packages */
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_search_file (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "search-group") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Search group");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_search_group (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "search-category") == 0) {
		if (value == NULL) {
			g_print ("specify a category\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Search category");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 2);

		/* add remote stores */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_search_category (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "what-conflicts") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Conflicts");


		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_what_obsoletes (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);

		goto out;
	}
	if (g_strcmp0 (mode, "what-obsoletes") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Obsoletes");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_what_obsoletes (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "what-provides") == 0) {
		if (value == NULL) {
			g_print ("specify a search term\n");
			goto out;
		}

		pk_progress_bar_start (progressbar, "Provides");

		/* setup state with the correct number of steps */
		zif_state_set_number_steps (state, 3);

		/* add both local and remote packages */
		store_array = zif_store_array_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_local (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
		if (!ret) {
			g_print ("failed to add remote: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		state_local = zif_state_get_child (state);
		to_array[0] = value;
		array = zif_store_array_what_provides (store_array, (gchar**)to_array, state_local, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, &error);
		if (!ret)
			goto out;

		/* no more progressbar */
		pk_progress_bar_end (progressbar);

		zif_print_packages (array);
		g_ptr_array_unref (array);
		goto out;
	}
	if (g_strcmp0 (mode, "update") == 0) {
		if (value == NULL) {
			g_print ("specify a package name\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Updating");
		zif_cmd_update (value, state);
		pk_progress_bar_end (progressbar);
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "get-upgrades") == 0) {
		ZifUpgrade *upgrade;
		pk_progress_bar_start (progressbar, "Getting upgrades");

		version = zif_config_get_uint (config, "releasever", NULL);
		array = zif_release_get_upgrades_new (release, version, state, &error);
		if (array == NULL) {
			g_warning ("FAILED: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* done with the bar */
		pk_progress_bar_end (progressbar);

		/* print the results */
		g_print ("Distribution upgrades available:\n");
		for (i=0; i<array->len; i++) {
			upgrade = g_ptr_array_index (array, i);
			if (!zif_upgrade_get_enabled (upgrade))
				continue;
			g_print ("%s\t[%s]\n",
				 zif_upgrade_get_id (upgrade),
				 zif_upgrade_get_stable (upgrade) ? "stable" : "unstable");
		}
		goto out;
	}
	if (g_strcmp0 (mode, "upgrade") == 0) {

		if (value == NULL) {
			g_print ("specify a distro name, e.g. 'fedora-9'\n");
			goto out;
		}
		pk_progress_bar_start (progressbar, "Upgrading");

		/* check valid */
		distro_id_split = g_strsplit (value, "-", -1);
		if (g_strv_length (distro_id_split) != 2) {
			g_warning ("distribution id invalid");
			goto out;
		}

		/* check fedora */
		if (g_strcmp0 (distro_id_split[0], "fedora") != 0) {
			g_warning ("only 'fedora' is supported");
			goto out;
		}

		/* check release */
		version = atoi (distro_id_split[1]);
		if (version < 13 || version > 99) {
			g_warning ("version number %i is invalid", version);
			goto out;
		}

		/* do the upgrade */
		ret = zif_release_upgrade_version (release, version,
						   ZIF_RELEASE_UPGRADE_KIND_DEFAULT,
						   state, &error);
		if (!ret) {
			g_warning ("FAILED: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* clean up after ourselves */
//		g_unlink ("/boot/upgrade/vmlinuz");
//		g_unlink ("/boot/upgrade/initrd.img");

		pk_progress_bar_end (progressbar);
		goto out;
	}
	
	g_print ("Nothing recognised\n");
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (store_local != NULL)
		g_object_unref (store_local);
	if (repos != NULL)
		g_object_unref (repos);
	if (config != NULL)
		g_object_unref (config);
	if (release != NULL)
		g_object_unref (release);
	if (state != NULL)
		g_object_unref (state);
	if (lock != NULL) {
		GError *error_local = NULL;
		ret = zif_lock_set_unlocked (lock, &error_local);
		if (!ret) {
			g_warning ("failed to unlock: %s", error_local->message);
			g_error_free (error_local);
		}
		g_object_unref (lock);
	}

	g_object_unref (progressbar);
	g_strfreev (distro_id_split);
	g_free (root);
	g_free (repos_dir);
	g_free (http_proxy);
	g_free (config_file);
	g_free (options_help);
	return 0;
}

