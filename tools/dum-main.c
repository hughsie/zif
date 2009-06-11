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
#include <dum.h>

#include "egg-debug.h"
#include "egg-string.h"

/**
 * dum_print_packages:
 **/
static void
dum_print_packages (GPtrArray *array)
{
	guint i;
	DumPackage *package;
	const PkPackageId *id;
	DumString *summary;

	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);
		id = dum_package_get_id (package);
		summary = dum_package_get_summary (package, NULL);
		g_print ("%s-%s.%s (%s)\t%s\n", id->name, id->version, id->arch, id->data, summary->value);
		dum_string_unref (summary);
	}
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GPtrArray *array;
	GPtrArray *updates;
	gboolean ret;
//	const gchar *id;
	DumRepos *repos = NULL;
	DumSack *sack = NULL;
	DumDownload *download = NULL;
	DumConfig *config = NULL;
	DumStoreLocal *store_local = NULL;
	DumStoreRemote *store_remote = NULL;
	DumGroups *groups = NULL;
	guint i, j;
	GError *error = NULL;
	DumPackage *package;
	const gchar *mode;
	const gchar *value;
	GOptionContext *context;
	gchar *options_help;
	gboolean verbose = FALSE;
	gboolean profile = FALSE;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "profile", 'p', 0, G_OPTION_ARG_NONE, &profile,
			_("Profile DUM"), NULL },
		{ NULL}
	};

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();
	dum_init ();

	context = g_option_context_new ("DUM Console Program");
	g_option_context_set_summary (context, 
		/* new */
		"  download       Download a package\n"
		"  getpackages    List all packages\n"
		"  getfiles       List the files in a package\n"
		"  resolve        Find a given package name\n"
		"  searchname     Search package name for the given string\n"
		"  searchdetails  Search package details for the given string\n"
		"  searchfile     Search packages for the given filename\n"
		"  searchgroup    Return packages in the given group\n"
		"  whatprovides   Find what package provides the given value\n"
		"  getdepends     List a package's dependencies\n"
		"  repolist       Display the configured software repositories\n"
		"  getdetails     Display details about a package or group of packages\n"
		"  clean          Remove cached data\n"
		"  get-updates    Check for available package updates\n"
		"  help           Display a helpful usage message\n"
		/* backwards compat */
		"\nThe following commands are provided for backwards compatibility.\n"
		"  resolvedep     Alias to whatprovides\n"
		"  search         Alias to searchdetails\n"
		"  deplist        Alias to getdepends\n"
		"  info           Alias to getdetails\n"
		"  list           Alias to getpackages\n"
		"  provides       Alias to whatprovides\n"
		"  check-update   Alias to get-updates\n"
		/* not even started yet */
		"\nThese won't work just yet...\n"
		"  refreshcache   Generate the metadata cache\n" /* new */
		"  makecache      Alias to refreshcache\n" /* backwards */
		"  upgrade        Alias to update\n" /* backwards */
		"  update         Update a package or packages on your system\n"
		"  reinstall      Reinstall a package\n"
		"  erase          Remove a package or packages from your system\n"
		"  install        Install a package or packages on your system\n"
		"  localinstall   Install a local RPM\n");

	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	/* verbose? */
	egg_debug_init (verbose);

	/* DumConfig */
	config = dum_config_new ();
	ret = dum_config_set_filename (config, "/etc/yum.conf", &error);
	if (!ret) {
		egg_error ("failed to set config: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* DumDownload */
	download = dum_download_new ();
	ret = dum_download_set_proxy (download, NULL, &error);
	if (!ret) {
		egg_error ("failed to set proxy: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* DumStoreLocal */
	store_local = dum_store_local_new ();
	ret = dum_store_local_set_prefix (store_local, "/", &error);
	if (!ret) {
		egg_error ("failed to set prefix: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* DumRepos */
	repos = dum_repos_new ();
	ret = dum_repos_set_repos_dir (repos, "/etc/yum.repos.d", &error);
	if (!ret) {
		egg_error ("failed to set repos dir: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* DumGroups */
	groups = dum_groups_new ();
	ret = dum_groups_set_mapping_file (groups, "/usr/share/PackageKit/helpers/yum/yum-comps-groups.conf", &error);
	if (!ret) {
		egg_error ("failed to set mapping file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	if (profile) {
		GTimer *timer;
		gdouble time_s;
		gdouble global = 0.0f;
		timer = g_timer_new ();

		/* load local sack */
		g_print ("load sack local... ");
		sack = DUM_SACK (dum_sack_local_new ());
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* resolve local sack */
		g_print ("resolve local sack... ");
		array = dum_sack_resolve (sack, "gnome-power-manager", &error);
		if (array == NULL || array->len == 0) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* resolve2 local sack */
		g_print ("resolve2 local sack... ");
		array = dum_sack_resolve (sack, "gnome-power-manager", &error);
		if (array == NULL || array->len == 0) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* searchfile local sack */
		g_print ("searchfile local sack... ");
		array = dum_sack_search_file (sack, "/usr/bin/gnome-power-manager", &error);
		if (array == NULL) {
			g_error ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* whatprovides local sack */
		g_print ("whatprovides local sack... ");
		array = dum_sack_what_provides (sack, "kernel", &error);
		if (array == NULL || array->len == 0) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
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
		sack = DUM_SACK (dum_sack_remote_new ());
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* resolve remote sack */
		g_print ("resolve remote sack... ");
		array = dum_sack_resolve (sack, "gnome-power-manager", &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* resolve2 remote sack */
		g_print ("resolve2 remote sack... ");
		array = dum_sack_resolve (sack, "gnome-power-manager", &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* searchfile remote sack */
		g_print ("searchfile remote sack... ");
		array = dum_sack_search_file (sack, "/usr/bin/gnome-power-manager", &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		time_s = g_timer_elapsed (timer, NULL);
		g_print ("\t\t : %lf\n", time_s);
		g_timer_reset (timer);
		global += time_s;

		/* whatprovides remote sack */
		g_print ("whatprovides remote sack... ");
		array = dum_sack_what_provides (sack, "kernel", &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
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

	/* DumSack */
	sack = DUM_SACK (dum_sack_local_new ());
	array = dum_repos_get_stores_enabled (repos, &error);
	if (array == NULL) {
		g_print ("failed to get enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	dum_sack_add_stores (sack, array);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);


	if (argc < 2) {
		g_print ("%s", options_help);
		goto out;
	}
	mode = argv[1];
	value = argv[2];
	if (g_strcmp0 (mode, "get-updates") == 0 || g_strcmp0 (mode, "check-update") == 0) {

		/* get all remote stores */
		array = dum_repos_get_stores_enabled (repos, &error);
		if (array == NULL) {
			g_print ("failed to get enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* get updates for each one */
		for (i=0; i<array->len; i++) {
			store_remote = DUM_STORE_REMOTE (g_ptr_array_index (array, i));
			updates = dum_store_remote_get_updates (store_remote, &error);
			if (updates == NULL) {
				g_print ("failed to get updates for store: %s\n", error->message);
				g_error_free (error);
				break;
			}
			g_print ("got updates for %s:\n", dum_store_get_id (DUM_STORE (store_remote)));
			dum_print_packages (updates);
			g_ptr_array_foreach (updates, (GFunc) g_object_unref, NULL);
			g_ptr_array_free (updates, TRUE);
		}

		/* free results */
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);

		goto out;
	}
	if (g_strcmp0 (mode, "clean") == 0) {

		/* get all remote stores */
		array = dum_repos_get_stores_enabled (repos, &error);
		if (array == NULL) {
			g_print ("failed to get enabled stores: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* clean each one */
		for (i=0; i<array->len; i++) {
			store_remote = DUM_STORE_REMOTE (g_ptr_array_index (array, i));
			ret = dum_store_remote_clean (store_remote, &error);
			if (!ret) {
				g_print ("failed to clean store: %s\n", error->message);
				g_error_free (error);
				break;
			}
			g_print ("Cleaned %s\n", dum_store_get_id (DUM_STORE (store_remote)));
		}

		/* free results */
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);

		goto out;
	}
	if (g_strcmp0 (mode, "getdepends") == 0 || g_strcmp0 (mode, "deplist") == 0) {
		DumDependArray *requires;
		DumDepend *require;
		gchar *require_str;
		GPtrArray *provides;
		const PkPackageId *id;

		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}
		array = dum_sack_resolve (sack, value, &error);
		if (array == NULL || array->len == 0) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		package = g_ptr_array_index (array, 0);

		requires = dum_package_get_requires (package, NULL);
		for (i=0; i<requires->value->len; i++) {
			require = g_ptr_array_index (requires->value, i);
			require_str = dum_depend_to_string (require);
			g_print ("  dependency: %s\n", require_str);
			g_free (require_str);

			provides = dum_sack_what_provides (sack, require->name, &error);
			if (provides == NULL) {
				g_print ("failed to get results: %s\n", error->message);
				g_error_free (error);
				goto out;
			}

			for (j=0;j<provides->len;j++) {
				package = g_ptr_array_index (provides, j);
				id = dum_package_get_id (package);
				g_print ("   provider: %s-%s.%s (%s)\n", id->name, id->version, id->arch, id->data);
			}
			g_ptr_array_foreach (provides, (GFunc) g_object_unref, NULL);
			g_ptr_array_free (provides, TRUE);
		}
		dum_depend_array_unref (requires);

		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "download") == 0) {
		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}
		array = dum_sack_resolve (sack, value, &error);
		if (array == NULL || array->len == 0) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		package = g_ptr_array_index (array, 2);
		ret = dum_package_download (package, "/tmp", &error);
		if (!ret) {
			g_print ("failed to download: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "erase") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "getfiles") == 0) {
		DumStringArray *files;

		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}

		/* resolve */
		array = dum_sack_resolve (sack, value, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* at least one result */
		if (array->len > 0) {
			package = g_ptr_array_index (array, 0);
			files = dum_package_get_files (package, NULL);
			for (i=0; i<files->value->len; i++)
				g_print ("%s\n", (const gchar *) g_ptr_array_index (files->value, i));
			dum_string_array_unref (files);
		} else {
			g_print ("Failed to match any packages to '%s'\n", value);
		}

		/* free results */
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
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
		DumString *summary;
		DumString *description;
		DumString *license;
		DumString *url;
		guint64 size;

		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}
		array = dum_sack_resolve (sack, value, &error);
		if (array == NULL || array->len == 0) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		package = g_ptr_array_index (array, 0);

		id = dum_package_get_id (package);
		summary = dum_package_get_summary (package, NULL);
		description = dum_package_get_description (package, NULL);
		license = dum_package_get_license (package, NULL);
		url = dum_package_get_url (package, NULL);
		size = dum_package_get_size (package, NULL);

		g_print ("Name\t : %s\n", id->name);
		g_print ("Version\t : %s\n", id->version);
		g_print ("Arch\t : %s\n", id->arch);
		g_print ("Size\t : %" G_GUINT64_FORMAT " bytes\n", size);
		g_print ("Repo\t : %s\n", id->data);
		g_print ("Summary\t : %s\n", summary->value);
		g_print ("URL\t : %s\n", url->value);
		g_print ("License\t : %s\n", license->value);
		g_print ("Description\t : %s\n", description->value);

		dum_string_unref (summary);
		dum_string_unref (url);
		dum_string_unref (license);
		dum_string_unref (description);

		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "install") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "list") == 0 || g_strcmp0 (mode, "getpackages") == 0) {
		array = dum_sack_get_packages (sack, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		dum_print_packages (array);
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "localinstall") == 0) {
		if (value == NULL) {
			g_print ("specify a filename");
			goto out;
		}
		package = DUM_PACKAGE (dum_package_local_new ());
		ret = dum_package_local_set_from_filename (DUM_PACKAGE_LOCAL (package), value /*"/home/hughsie/rpmbuild/REPOS/fedora/11/i386/brew-builder-0.9.4-4.fc11.noarch.rpm"*/, &error);
		if (!ret)
			egg_error ("failed: %s", error->message);
		dum_package_print (package);
		g_object_unref (package);
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "makecache") == 0 || g_strcmp0 (mode, "refreshcache") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "reinstall") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	if (g_strcmp0 (mode, "repolist") == 0) {
		/* get list */
		array = dum_repos_get_stores (repos, &error);
		if (array == NULL) {
			g_print ("failed to get list of repos: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* print */
		for (i=0; i<array->len; i++) {
			store_remote = g_ptr_array_index (array, i);
			g_print ("%s\t\t%s\t\t%s\n",
				 dum_store_get_id (DUM_STORE (store_remote)),
				 dum_store_remote_get_enabled (store_remote, NULL) ? "enabled" : "disabled",
				 dum_store_remote_get_name (store_remote, NULL));
		}

		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "resolve") == 0) {
		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}
		array = dum_sack_resolve (sack, value, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		dum_print_packages (array);
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "searchname") == 0) {
		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}
		array = dum_sack_search_name (sack, value, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		dum_print_packages (array);
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "searchdetails") == 0 || g_strcmp0 (mode, "search") == 0) {
		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}
		array = dum_sack_search_details (sack, value, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		dum_print_packages (array);
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "searchfile") == 0) {
		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}
		array = dum_sack_search_file (sack, value, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		dum_print_packages (array);
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "searchgroup") == 0) {
		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}
		array = dum_sack_search_group (sack, value, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		dum_print_packages (array);
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "resolvedep") == 0 || g_strcmp0 (mode, "whatprovides") == 0 || g_strcmp0 (mode, "resolvedep") == 0 || g_strcmp0 (mode, "provides") == 0) {
		if (value == NULL) {
			g_print ("specify a value");
			goto out;
		}
		array = dum_sack_what_provides (sack, value, &error);
		if (array == NULL) {
			g_print ("failed to get results: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		dum_print_packages (array);
		g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (array, TRUE);
		goto out;
	}
	if (g_strcmp0 (mode, "update") == 0 || g_strcmp0 (mode, "upgrade") == 0) {
		g_print ("not yet supported\n");
		goto out;
	}
	
	g_print ("Nothing recognised\n");
out:
	if (sack != NULL)
		g_object_unref (sack);
	if (download != NULL)
		g_object_unref (download);
//	if (store_local != NULL)
//		g_object_unref (store_local);
	if (repos != NULL)
		g_object_unref (repos);
	if (config != NULL)
		g_object_unref (config);
	g_free (options_help);
	return 0;
}

