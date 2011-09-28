/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
.* Copyright (C) 2011 Elad Alfassa <elad@fedoraproject.org>
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

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib.h>
#include <glib-unix.h>

#include <locale.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <zif.h>

#include "zif-progress-bar.h"

/**
 * zif_print_package:
 **/
static void
zif_print_package (ZifPackage *package, guint padding)
{
	const gchar *printable;
	const gchar *summary;
	const gchar *trusted_str = "";
	gchar *padding_str;
	ZifState *state_tmp;
	ZifPackageTrustKind trust;
	ZifPackage *installed = NULL;

	printable = zif_package_get_printable (package);
	state_tmp = zif_state_new ();
	summary = zif_package_get_summary (package, state_tmp, NULL);
	if (padding > 0) {
		padding_str = g_strnfill (padding - strlen (printable), ' ');
	} else {
		padding_str = g_strnfill (2, ' ');
	}
	trust = zif_package_get_trust_kind (package);
	if (trust == ZIF_PACKAGE_TRUST_KIND_PUBKEY) {
		trusted_str = _("[⚐]");
	} else if (trust == ZIF_PACKAGE_TRUST_KIND_NONE) {
		trusted_str = _("[⚑]");
	}
	g_print ("%s %s%s%s\n",
		 printable,
		 trusted_str,
		 padding_str,
		 summary);

	/* print installed package info */
	if (ZIF_IS_PACKAGE_REMOTE (package)) {
		installed = zif_package_remote_get_installed (ZIF_PACKAGE_REMOTE (package));
		if (installed != NULL) {
			g_print (" - %s: %s\n",
				 _("Updates installed package"),
				 zif_package_get_printable (installed));
			g_object_unref (installed);
		}
	}
	g_free (padding_str);
	g_object_unref (state_tmp);
}

/**
 * zif_package_sort_cb:
 **/
static gint
zif_package_sort_cb (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (zif_package_get_id (*(ZifPackage **)a),
			  zif_package_get_id (*(ZifPackage **)b));
}

/**
 * zif_print_packages:
 **/
static void
zif_print_packages (GPtrArray *array)
{
	guint i, j;
	guint max = 0;
	const gchar *printable;
	ZifPackage *package;

	if (array->len == 0) {
		/* TRANSLATORS: there are no packages that match */
		g_print ("%s\n", _("There are no packages to show."));
		return;
	}

	/* sort the array */
	g_ptr_array_sort (array, zif_package_sort_cb);

	/* get the padding required */
	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);
		printable = zif_package_get_printable (package);
		j = strlen (printable);
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
zif_state_percentage_changed_cb (ZifState *state, guint percentage, ZifProgressBar *progressbar)
{
	zif_progress_bar_set_percentage (progressbar, percentage);
}

/**
 * zif_state_allow_cancel_changed_cb:
 **/
static void
zif_state_allow_cancel_changed_cb (ZifState *state, gboolean allow_cancel, ZifProgressBar *progressbar)
{
	zif_progress_bar_set_allow_cancel (progressbar, allow_cancel);
}

/**
 * zif_state_action_to_string_localized:
 **/
static const gchar *
zif_state_action_to_string_localized (ZifStateAction action)
{
	if (action == ZIF_STATE_ACTION_CHECKING) {
		/* TRANSLATORS: this is when files, usually metadata or
		 * package files are being checked for consitency */
		return _("Checking");
	}
	if (action == ZIF_STATE_ACTION_DOWNLOADING) {
		/* TRANSLATORS: A file is currently downloading */
		return _("Downloading");
	}
	if (action == ZIF_STATE_ACTION_LOADING_REPOS) {
		/* TRANSLATORS: A repository file is being read, and
		 * the packages created internally */
		return _("Loading repository");
	}
	if (action == ZIF_STATE_ACTION_DECOMPRESSING) {
		/* TRANSLATORS: when a compressed metadata file is
		 * being uncompressed onto the disk */
		return _("Decompressing");
	}
	if (action == ZIF_STATE_ACTION_DEPSOLVING_INSTALL) {
		/* TRANSLATORS: when the transaction is being resolved,
		 * and we make sure that it makes sense by adding
		 * dependencies where required */
		return _("Calculating install");
	}
	if (action == ZIF_STATE_ACTION_DEPSOLVING_REMOVE) {
		/* TRANSLATORS: when the transaction is being resolved,
		 * and we make sure that it makes sense by removing
		 * dependencies where required */
		return _("Calculating removal");
	}
	if (action == ZIF_STATE_ACTION_DEPSOLVING_UPDATE) {
		/* TRANSLATORS: when the transaction is being resolved,
		 * and we make sure that it makes sense by adding and
		 * removing dependencies where required */
		return _("Calculating update");
	}
	if (action == ZIF_STATE_ACTION_DEPSOLVING_CONFLICTS) {
		/* TRANSLATORS: when the transaction is being checked
		 * for conflicting packages */
		return _("Checking conflicts");
	}
	if (action == ZIF_STATE_ACTION_INSTALLING) {
		/* TRANSLATORS: installing a package to the local system */
		return _("Installing");
	}
	if (action == ZIF_STATE_ACTION_REMOVING) {
		/* TRANSLATORS: removing (deleting) a package */
		return _("Removing");
	}
	if (action == ZIF_STATE_ACTION_UPDATING) {
		/* TRANSLATORS: updating an old version to a new version */
		return _("Updating");
	}
	if (action == ZIF_STATE_ACTION_CLEANING) {
		/* TRANSLATORS: Cleaning up after an update, where we
		 * remove the old version */
		return _("Cleaning");
	}
	if (action == ZIF_STATE_ACTION_PREPARING) {
		/* TRANSLATORS: getting ready to do run the transaction,
		 * doing things like checking the database and checking
		 * for file conflicts */
		return _("Preparing");
	}
	if (action == ZIF_STATE_ACTION_TEST_COMMIT) {
		/* TRANSLATORS: checking the transaction for file
		 * conflicts after packages have been downloaded*/
		return _("Testing");
	}
	if (action == ZIF_STATE_ACTION_LOADING_RPMDB) {
		/* TRANSLATORS: loading the rpmdb */
		return _("Loading installed");
	}
	if (action == ZIF_STATE_ACTION_CHECKING_UPDATES) {
		/* TRANSLATORS: calculating the update set */
		return _("Checking updates");
	}
	return zif_state_action_to_string (action);
}

/**
 * zif_main_elipsize_middle_sha1:
 **/
static void
zif_main_elipsize_middle_sha1 (gchar *filename)
{
	const guint hash_ends = 6; /* show this many chars between the "..." */
	const guint hash_len = 64; /* sha1 */
	guint len;

	/* if this is a sha1-has prefixed filename like:
	 * c723889aaa8c330b63397982a0bf012b78ed1c94a907ff96a1a7ba16c08bcb1e-primary.sqlite.bz2
	 * then reduce it down to something like:
	 * c723...cb1e-primary.sqlite.bz2 */
	len = strlen (filename);
	if (len > hash_len + 1 &&
	    filename[hash_len] == '-') {
		g_strlcpy (filename + hash_ends,
			   "...",
			   len);
		g_strlcpy (filename + hash_ends + 3,
			   filename + hash_len - hash_ends,
			   len);
	}
}

/**
 * zif_state_action_changed_cb:
 **/
static void
zif_state_action_changed_cb (ZifState *state,
			     ZifStateAction action,
			     const gchar *action_hint,
			     ZifProgressBar *progressbar)
{
	gchar *pretty_hint = NULL;
	gchar **split = NULL;
	guint len;

	/* show nothing for hint cancel */
	if (action == ZIF_STATE_ACTION_UNKNOWN) {
		zif_progress_bar_set_action (progressbar, NULL);
		goto out;
	}

	/* new action */
	zif_progress_bar_set_action (progressbar,
				     zif_state_action_to_string_localized (action));

	if (action_hint == NULL) {
		/* ignore */

	/* only show basename for filenames */
	} else if (action_hint[0] == '/') {

		split = g_strsplit (action_hint, "/", -1);
		len = g_strv_length (split);
		if (len > 2) {
			zif_main_elipsize_middle_sha1 (split[len-1]);
			pretty_hint = g_build_filename (split[len-2],
							split[len-1],
							NULL);
		} else {
			pretty_hint = g_strdup (action_hint);
		}

	/* show nice name for package */
	} else if (zif_package_id_check (action_hint)) {

		pretty_hint = zif_package_id_get_printable (action_hint);

	/* fallback to just showing it */
	} else {
		pretty_hint = g_strdup (action_hint);
	}
	zif_progress_bar_set_detail (progressbar, pretty_hint);
out:
	g_strfreev (split);
	g_free (pretty_hint);
}


/**
 * zif_state_speed_changed_cb:
 **/
static void
zif_state_speed_changed_cb (ZifState *state,
			    GParamSpec *pspec,
			    ZifProgressBar *progressbar)
{
	zif_progress_bar_set_speed (progressbar,
				    zif_state_get_speed (state));
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

	/* not a console */
	if (isatty (fileno (stdout)) == 0) {
		g_print ("%s\n", message);
		return;
	}

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

typedef struct {
	gboolean		 assume_no;
	GOptionContext		*context;
	GPtrArray		*cmd_array;
	ZifConfig		*config;
	ZifProgressBar		*progressbar;
	ZifRepos		*repos;
	ZifState		*state;
	ZifStore		*store_local;
	guint			 uid;
	gchar			*cmdline;
} ZifCmdPrivate;

typedef gboolean (*ZifCmdPrivateCb)	(ZifCmdPrivate	*cmd,
					 gchar		**values,
					 GError		**error);

typedef struct {
	gchar		*name;
	gchar		*description;
	ZifCmdPrivateCb	 callback;
} ZifCmdItem;

/**
 * zif_cmd_item_free:
 **/
static void
zif_cmd_item_free (ZifCmdItem *item)
{
	g_free (item->name);
	g_free (item->description);
	g_free (item);
}

/*
 * zif_sort_command_name_cb:
 */
static gint
zif_sort_command_name_cb (ZifCmdItem **item1, ZifCmdItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

/**
 * zif_cmd_add:
 **/
static void
zif_cmd_add (GPtrArray *array, const gchar *name, const gchar *description, ZifCmdPrivateCb callback)
{
	gchar **names;
	guint i;
	ZifCmdItem *item;

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (i=0; names[i] != NULL; i++) {
		item = g_new0 (ZifCmdItem, 1);
		item->name = g_strdup (names[i]);
		if (i == 0) {
			item->description = g_strdup (description);
		} else {
			/* TRANSLATORS: this is a command alias */
			item->description = g_strdup_printf ("Alias to %s",
							     names[0]);
		}
		item->callback = callback;
		g_ptr_array_add (array, item);
	}
	g_strfreev (names);
}

/**
 * zif_cmd_get_descriptions:
 **/
static gchar *
zif_cmd_get_descriptions (GPtrArray *array)
{
	guint i;
	guint j;
	guint len;
	guint max_len = 19;
	ZifCmdItem *item;
	GString *string;

	/* print each command */
	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		g_string_append (string, " ");
		len = strlen (item->name);
		for (j=len; j<max_len+2; j++)
			g_string_append_c (string, ' ');
		g_string_append (string, item->description);
		g_string_append_c (string, '\n');
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * zif_cmd_clean:
 **/
static gboolean
zif_cmd_clean (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ZifState *state_local;
	GPtrArray *store_array = NULL;

	/* TRANSLATORS: we're cleaning the repo, deleting old files */
	zif_progress_bar_start (priv->progressbar, _("Cleaning"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1,
				   99,
				   -1);
	if (!ret)
		goto out;

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* clean all the store_array */
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_clean (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_filter_post_resolve:
 **/
static gboolean
zif_filter_post_resolve (ZifCmdPrivate *priv,
			 GPtrArray *array,
			 GError **error)
{
	gboolean exactarch;
	gchar *archinfo = NULL;
	gboolean ret = FALSE;

	/*no input */
	if (array->len == 0) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error_literal (error, 1, 0,
				     _("No packages found"));
		goto out;
	}

	/* is the exact arch required? */
	exactarch = zif_config_get_boolean (priv->config,
					    "exactarch", error);
	if (*error != NULL)
		goto out;

	/* be more harsh if we're exactarch */
	archinfo = zif_config_get_string (priv->config,
					  "archinfo", error);
	if (archinfo == NULL)
		goto out;
	if (exactarch)
		zif_package_array_filter_arch (array, archinfo);

	/* we only want the newest version */
	zif_package_array_filter_newest (array);

	/* eek, nothing left */
	if (array->len == 0) {
		/* TRANSLATORS: error message */
		g_set_error_literal (error, 1, 0,
				     _("No packages found (after filter)"));
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	g_free (archinfo);
	return ret;
}

/**
 * zif_cmd_download:
 **/
static gboolean
zif_cmd_download (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;
	GPtrArray *store_array = NULL;

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   30,
				   30,
				   40,
				   -1);
	if (!ret)
		goto out;

	/* add remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* resolve package name */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, _("No %s package was found"), values[0]);
		goto out;
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* TRANSLATORS: downloading packages */
	zif_progress_bar_start (priv->progressbar, _("Downloading"));

	/* download package file */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, array->len);
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		state_loop = zif_state_get_child (state_local);
		ret = zif_package_remote_download (ZIF_PACKAGE_REMOTE (package),
						   "/tmp", state_loop, error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* progress */
	zif_progress_bar_end (priv->progressbar);

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_find_package:
 **/
static gboolean
zif_cmd_find_package (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *store_array = NULL;
	ZifPackage *package = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a package_id");
		goto out;
	}

	/* TRANSLATORS: finding packages in local and remote repos */
	zif_progress_bar_start (priv->progressbar, _("Finding package"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   80,
				   10,
				   10,
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* get id */
	if (!zif_package_id_check (values[0])) {
		g_set_error (error, 1, 0, "failed to parse ID: %s", values[0]);
		goto out;
	}

	/* find package id */
	state_local = zif_state_get_child (priv->state);
	package = zif_store_array_find_package (store_array, values[0], state_local, error);
	if (package == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	zif_print_package (package, 0);

	/* success */
	ret = TRUE;
out:
	if (package != NULL)
		g_object_unref (package);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_get_categories:
 **/
static gboolean
zif_cmd_get_categories (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	const gchar *cat_id;
	const gchar *parent_id;
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	guint j;
	guint len;
	guint len_max = 0;
	ZifCategory *obj;
	ZifState *state_local;

	/* TRANSLATORS: getting the hierarchical groups from the server */
	zif_progress_bar_start (priv->progressbar, _("Getting categories"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   50,
				   50,
				   -1);
	if (!ret)
		goto out;

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* get categories */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_get_categories (store_array, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* get padding */
	for (i=0; i<array->len; i++) {
		obj = g_ptr_array_index (array, i);
		parent_id = zif_category_get_parent_id (obj);
		cat_id = zif_category_get_id (obj);
		if (parent_id != NULL)
			len = strlen (parent_id) + strlen (cat_id) + 1;
		else
			len = strlen (cat_id);
		if (len > len_max)
			len_max = len;
	}

	/* dump to console */
	for (i=0; i<array->len; i++) {
		obj = g_ptr_array_index (array, i);
		parent_id = zif_category_get_parent_id (obj);
		cat_id = zif_category_get_id (obj);
		if (parent_id != NULL)
			len = strlen (parent_id) + strlen (cat_id) + 1;
		else
			len = strlen (cat_id);
		if (parent_id != NULL) {
			g_print ("%s/%s",
				 parent_id, cat_id);
		} else {
			g_print ("%s", cat_id);
		}
		for (j=len; j<len_max + 1; j++)
			g_print (" ");
		g_print ("%s",
			 zif_category_get_name (obj));
		if (zif_category_get_summary (obj) != NULL) {
			g_print (" (%s)",
				 zif_category_get_summary (obj));
		}
		g_print ("\n");
	}

	/* success */
	ret = TRUE;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_get_depends:
 **/
static gboolean
zif_cmd_get_depends (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	ZifState *state_local;
	ZifPackage *package;
	GPtrArray *requires = NULL;
	ZifDepend *require;
	const gchar *require_str;
	GPtrArray *provides = NULL;
	guint i, j;
	GString *string = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: A user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a package name"));
		goto out;
	}

	/* TRANSLATORS: getting the list pf package dependencies for a package */
	zif_progress_bar_start (priv->progressbar, _("Getting depends"));

	/* use a temp string to get output results */
	string = g_string_new ("");

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   25, /* add local and remote */
				   25, /* resolve */
				   25, /* get requires */
				   25, /* what requires loop */
				   -1);
	if (!ret)
		goto out;

	/* add all stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* resolve package name */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array,
					      (gchar**)values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "no package found");
		goto out;
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	package = g_ptr_array_index (array, 0);

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* get requires */
	state_local = zif_state_get_child (priv->state);
	requires = zif_package_get_requires (package, state_local, error);
	if (requires == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* match a package to each require */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, requires->len);

	/* setup deeper state */
	for (i=0; i<requires->len; i++) {
		require = g_ptr_array_index (requires, i);
		require_str = zif_depend_get_description (require);
		/* TRANSLATORS: this is a item prefix */
		g_string_append_printf (string, "  %s %s\n", _("Dependency:"), require_str);
	}

	/* find the packages providing the depends */
	state_local = zif_state_get_child (priv->state);
	provides = zif_store_array_what_provides (store_array, requires, state_local, error);
	if (provides == NULL) {
		ret = FALSE;
		goto out;
	}

	/* print all of them */
	for (j=0;j<provides->len;j++) {
		package = g_ptr_array_index (provides, j);
		/* TRANSLATORS: this is a item prefix */
		g_string_append_printf (string, "   %s %s\n",
					_("Provider:"),
					zif_package_get_printable (package));
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* no more progressbar */
	zif_progress_bar_end (priv->progressbar);

	/* success */
	g_print ("%s", string->str);
	ret = TRUE;
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	if (provides != NULL)
		g_ptr_array_unref (provides);
	if (requires != NULL)
		g_ptr_array_unref (requires);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_get_details:
 **/
static gboolean
zif_cmd_get_details (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	const gchar *description;
	const gchar *license;
	const gchar *summary;
	const gchar *url;
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	GString *string = NULL;
	guint64 size;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	/* TRANSLATORS: getting the details (summary, size, etc) of a package */
	zif_progress_bar_start (priv->progressbar, _("Getting details"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   25,
				   25,
				   25,
				   25,
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: A user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a package name"));
		goto out;
	}
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	if (array->len == 0) {
		/* TRANSLATORS: error message: nothing was found */
		g_set_error_literal (error, 1, 0,
				     _("No package was found"));
		goto out;
	}

	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, array->len);

	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);

		state_loop = zif_state_get_child (state_local);
		summary = zif_package_get_summary (package, state_loop, NULL);
		description = zif_package_get_description (package, state_loop, NULL);
		license = zif_package_get_license (package, state_loop, NULL);
		url = zif_package_get_url (package, state_loop, NULL);
		size = zif_package_get_size (package, state_loop, NULL);

		/* TRANSLATORS: these are headers for the package data */
		g_string_append_printf (string, "%s\t : %s\n", _("Name"), zif_package_get_name (package));
		g_string_append_printf (string, "%s\t : %s\n", _("Version"), zif_package_get_version (package));
		g_string_append_printf (string, "%s\t : %s\n", _("Arch"), zif_package_get_arch (package));
		g_string_append_printf (string, "%s\t : %" G_GUINT64_FORMAT " bytes\n", _("Size"), size);
		g_string_append_printf (string, "%s\t : %s\n", _("Repo"), zif_package_get_data (package));
		g_string_append_printf (string, "%s\t : %s\n", _("Summary"), summary);
		g_string_append_printf (string, "%s\t : %s\n", _("URL"), url);
		g_string_append_printf (string, "%s\t : %s\n", _("License"), license);
		g_string_append_printf (string, "%s\t : %s\n", _("Description"), description);

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* print what we've got */
	zif_progress_bar_end (priv->progressbar);
	g_print ("%s", string->str);

	/* success */
	ret = TRUE;
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_dep_common:
 **/
static gboolean
zif_cmd_dep_common (ZifCmdPrivate *priv, ZifPackageEnsureType type,
		    gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *depends = NULL;
	GPtrArray *store_array = NULL;
	GString *string = NULL;
	guint i, j;
	ZifDepend *depend;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	/* TRANSLATORS: getting the depends of a package */
	zif_progress_bar_start (priv->progressbar, _("Getting depends"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   25,
				   25,
				   25,
				   25,
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: A user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a package name"));
		goto out;
	}
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	if (array->len == 0) {
		/* TRANSLATORS: error message: nothing was found */
		g_set_error_literal (error, 1, 0,
				     _("No package was found"));
		goto out;
	}

	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, array->len);

	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_string_append_printf (string, "%s\n",
					zif_package_get_printable (package));

		state_loop = zif_state_get_child (state_local);
		if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {
			depends = zif_package_get_provides (package,
							    state_loop,
							    error);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {
			depends = zif_package_get_requires (package,
							    state_loop,
							    error);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {
			depends = zif_package_get_conflicts (package,
							     state_loop,
							     error);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {
			depends = zif_package_get_obsoletes (package,
							     state_loop,
							     error);
		} else {
			g_assert_not_reached ();
		}
		if (depends == NULL) {
			ret = FALSE;
			goto out;
		}

		zif_progress_bar_end (priv->progressbar);
		for (j=0; j<depends->len; j++) {
			depend = g_ptr_array_index (depends, j);
			if (g_str_has_prefix (zif_depend_get_name (depend), "/"))
				continue;
			g_string_append_printf (string, "\t%s\n",
						zif_depend_get_description (depend));
		}

		g_ptr_array_unref (depends);
		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	g_print ("%s\n", string->str);

	/* success */
	ret = TRUE;
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_dep_provides:
 **/
static gboolean
zif_cmd_dep_provides (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	return zif_cmd_dep_common (priv,
				   ZIF_PACKAGE_ENSURE_TYPE_PROVIDES,
				   values,
				   error);
}

/**
 * zif_cmd_dep_requires:
 **/
static gboolean
zif_cmd_dep_requires (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	return zif_cmd_dep_common (priv,
				   ZIF_PACKAGE_ENSURE_TYPE_REQUIRES,
				   values,
				   error);
}

/**
 * zif_cmd_dep_conflicts:
 **/
static gboolean
zif_cmd_dep_conflicts (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	return zif_cmd_dep_common (priv,
				   ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS,
				   values,
				   error);
}

/**
 * zif_cmd_dep_obsoletes:
 **/
static gboolean
zif_cmd_dep_obsoletes (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	return zif_cmd_dep_common (priv,
				   ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES,
				   values,
				   error);
}

/**
 * zif_get_localised_date:
 **/
static gchar *
zif_get_localised_date (guint timestamp)
{
	struct tm *tmp;
	gchar buffer[100];
	time_t timet = timestamp;

	/* get printed string */
	tmp = localtime (&timet);

	/* TRANSLATORS: strftime formatted please */
	strftime (buffer, 100, _("%F %R"), tmp);
	return g_strdup (buffer);
}

/**
 * zif_cmd_history_list:
 **/
static gboolean
zif_cmd_history_list (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	GArray *transactions;
	gboolean important = TRUE;
	gboolean ret = FALSE;
	gchar *cmdline;
	gchar *date_str;
	gchar *reason_pad;
	GPtrArray *packages;
	guint i, j;
	guint timestamp;
	guint uid;
	ZifHistory *history;
	ZifPackage *package;
	ZifTransactionReason reason;

	/* open the history */
	history = zif_history_new ();

	/* get all transactions */
	transactions = zif_history_list_transactions (history, error);
	if (transactions == NULL)
		goto out;

	/* no entries */
	if (transactions->len == 0) {
		ret = TRUE;
		g_print ("%s\n", _("The history database is empty"));
		goto out;
	}

	/* list them */
	for (i = 0; i < transactions->len; i++) {
		timestamp = g_array_index (transactions, guint, i);

		/* get packages */
		packages = zif_history_get_packages (history,
						     timestamp,
						     error);
		if (packages == NULL)
			goto out;

		/* filter by package */
		if (values != NULL && values[0] != NULL) {
			important = FALSE;
			for (j = 0; j < packages->len; j++) {
				package = g_ptr_array_index (packages, j);
				if (g_strcmp0 (zif_package_get_name (package), values[0]) == 0) {
					important = TRUE;
					break;
				}
			}
			if (!important)
				continue;
		}

		/* print transaction */
		date_str = zif_get_localised_date (timestamp);
		g_print ("%s #%i, %s\n", _("Transaction"), i, date_str);

		for (j = 0; j < packages->len; j++) {
			package = g_ptr_array_index (packages, j);
			uid = zif_history_get_uid (history, package, timestamp, NULL);
			cmdline = zif_history_get_cmdline (history, package, timestamp, NULL);
			reason = zif_history_get_reason (history, package, timestamp, NULL);
			reason_pad = zif_strpad (zif_transaction_reason_to_string (reason), 20);
			g_print ("\t%s\t%s (user %i with %s)\n",
				 reason_pad,
				 zif_package_get_printable (package),
				 uid,
				 cmdline);
			g_free (cmdline);
		}

		g_free (date_str);
		g_ptr_array_unref (packages);
	}

	/* success */
	ret= TRUE;
out:
	if (transactions != NULL)
		g_array_unref (transactions);
	g_object_unref (history);
	return ret;
}

/**
 * zif_cmd_history_import:
 **/
static gboolean
zif_cmd_history_import (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ZifDb *db;
	ZifHistory *history;

	/* open the history and database */
	history = zif_history_new ();
	db = zif_db_new ();

	/* import entries */
	ret = zif_history_import (history, db, error);
	if (!ret)
		goto out;

	/* TRANSLATORS: we've imported the yumdb into the history database */
	g_print ("%s\n", _("All database entries imported into history"));
out:
	g_object_unref (history);
	g_object_unref (db);
	return ret;
}

/*
 * zif_sort_indirect_strcmp_cb:
 */
static gint
zif_sort_indirect_strcmp_cb (const gchar **file1, const gchar **file2)
{
	return g_strcmp0 (*file1, *file2);
}

/**
 * zif_cmd_get_files:
 **/
static gboolean
zif_cmd_get_files (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	const gchar *filename;
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *files = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	guint j;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_tmp;
	GString *string = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: user needs to specify a value */
		g_set_error (error, 1, 0, _("Specify a package name"));
		goto out;
	}

	/* TRANSLATORS: getting file lists for a package */
	zif_progress_bar_start (priv->progressbar, _("Getting files"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   2, /* add local */
				   2, /* add remote */
				   50, /* resolve */
				   46, /* get files */
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* resolve */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* at least one result */
	if (array->len == 0) {
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, "%s %s",
			     _("Failed to match any packages for :"),
			     values[0]);
		goto out;
	}

	/* get string */
	string = g_string_new ("");
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, array->len);
	for (j=0; j<array->len; j++) {
		package = g_ptr_array_index (array, j);
		g_string_append_printf (string, "Package %s\n",
					zif_package_get_printable (package));
		state_tmp = zif_state_get_child (state_local);
		files = zif_package_get_files (package, state_tmp, error);
		if (files == NULL) {
			ret = FALSE;
			goto out;
		}
		if (files->len == 0) {
			/* TRANSLATORS: printed when a package has no files */
			g_string_append_printf (string, "%s\n",
						_("Package contains no files"));
		} else {
			/* sort by name */
			g_ptr_array_sort (files,
					  (GCompareFunc) zif_sort_indirect_strcmp_cb);
			for (i=0; i<files->len; i++) {
				filename = g_ptr_array_index (files, i);
				g_string_append_printf (string, " - %s\n",
							filename);
			}
		}
		g_string_append (string, "\n");

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* print */
	g_print ("%s", string->str);

	/* success */
	ret = TRUE;
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (files != NULL)
		g_ptr_array_unref (files);
	return ret;
}

/**
 * zif_cmd_get_groups:
 **/
static gboolean
zif_cmd_get_groups (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	const gchar *text;
	gboolean ret;
	GPtrArray *array = NULL;
	guint i;
	ZifGroups *groups;

	/* ZifGroups */
	groups = zif_groups_new ();
	ret = zif_groups_set_mapping_file (groups, "/usr/share/PackageKit/helpers/yum/yum-comps-groups.conf", error);
	if (!ret)
		goto out;

	/* get bitfield */
	array = zif_groups_get_groups (groups, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* convert to text */
	for (i=0; i<array->len; i++) {
		text = g_ptr_array_index (array, i);
		g_print ("%s\n", text);
	}

	/* success */
	ret = TRUE;
out:
	if (groups != NULL)
		g_object_unref (groups);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_get_packages:
 **/
static gboolean
zif_cmd_get_packages (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	ZifState *state_local;

	/* TRANSLATORS: getting all the packages */
	zif_progress_bar_start (priv->progressbar, _("Getting packages"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   80,
				   10,
				   10,
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_get_packages (store_array, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_get_update_array:
 **/
static GPtrArray *
zif_get_update_array (ZifCmdPrivate *priv, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *store_array = NULL;
	GPtrArray *updates = NULL;
	ZifState *state_local;

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (state,
				   error,
				   2, /* add remote */
				   98, /* get updates */
				   -1);
	if (!ret)
		goto out;

	/* add remote stores to array */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get the updates list */
	state_local = zif_state_get_child (state);
	updates = zif_store_array_get_updates (store_array,
					       priv->store_local,
					       state_local,
					       error);
	if (updates == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return updates;
}

/**
 * zif_cmd_get_updates:
 **/
static gboolean
zif_cmd_get_updates (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	const guint metadata_expire_1day = 60 * 60 * 24; /* 24h */
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	guint metadata_expire;

	/* TRANSLATORS: getting the list of packages that can be updated */
	zif_progress_bar_start (priv->progressbar, _("Getting updates"));

	/* force the metadata timeout to be at most 24h */
	metadata_expire = zif_config_get_uint (priv->config,
					       "metadata_expire",
					       error);
	if (metadata_expire == G_MAXUINT)
		goto out;
	if (metadata_expire > metadata_expire_1day) {
		g_debug ("overriding metadata_expire from %i to %i",
			 metadata_expire,
			 metadata_expire_1day);
		zif_config_unset (priv->config,
				  "metadata_expire",
				  NULL);
		ret = zif_config_set_uint (priv->config,
					   "metadata_expire",
					   metadata_expire_1day,
					   error);
		if (!ret)
			goto out;
	}

	/* get the update list */
	array = zif_get_update_array (priv, priv->state, error);
	if (array == NULL)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (array);

	/* success */
	ret = TRUE;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_get_upgrades:
 **/
static gboolean
zif_cmd_get_upgrades (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	guint i;
	ZifRelease *release = NULL;
	ZifUpgrade *upgrade;

	/* TRANSLATORS: getting details of any distro upgrades */
	zif_progress_bar_start (priv->progressbar, _("Getting upgrades"));

	release = zif_release_new ();
	array = zif_release_get_upgrades_new (release, priv->state, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* done with the bar */
	zif_progress_bar_end (priv->progressbar);

	/* print the results */
	if (array->len == 0) {
		g_print ("%s\n", _("No distribution upgrades are available."));
	} else {
		g_print ("%s\n", _("Distribution upgrades available:"));
		for (i=0; i<array->len; i++) {
			upgrade = g_ptr_array_index (array, i);
			if (!zif_upgrade_get_enabled (upgrade))
				continue;
			g_print ("%s\t[%s]\n",
				 zif_upgrade_get_id (upgrade),
				 zif_upgrade_get_stable (upgrade) ? _("stable") : _("unstable"));
		}
	}

	/* success */
	ret = TRUE;
out:
	if (release != NULL)
		g_object_unref (release);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_get_config_value:
 **/
static gboolean
zif_cmd_get_config_value (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	gchar *value = NULL;
	guint i;
	ZifState *state_local;

	/* it might seem odd to open and load the local store here, but
	 * we need to have set the releasever */
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_load (priv->store_local, state_local, error);
	if (!ret)
		goto out;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: A user didn't specify a required value */
		g_set_error_literal (error, 1, 0, _("Specify a config key"));
		goto out;
	}

	/* get value */
	for (i=0; values[i] != NULL; i++) {
		value = zif_config_get_string (priv->config, values[i], NULL);
		if (value == NULL) {
			/* TRANSLATORS: there was no value in the config files */
			g_set_error (error, 1, 0, _("No value for %s"), values[i]);
			goto out;
		}

		/* print the results */
		g_print ("%s = '%s'\n", values[i], value);
		g_free (value);
	}

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_cmd_help:
 **/
static gboolean
zif_cmd_help (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gchar *options_help;
	options_help = g_option_context_get_help (priv->context, TRUE, NULL);
	g_print ("%s\n", options_help);
	g_free (options_help);
	return TRUE;
}

/**
 * zif_cmd_getchar_unbuffered:
 **/
static gchar
zif_cmd_getchar_unbuffered (void)
{
	gchar c = '\0';
	struct termios org_opts, new_opts;
	gint res = 0;

	/* store old settings */
	res = tcgetattr (STDIN_FILENO, &org_opts);
	if (res != 0)
		g_warning ("failed to set terminal");

	/* set new terminal parms */
	memcpy (&new_opts, &org_opts, sizeof(new_opts));
	new_opts.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOPRT | ECHOKE | ICRNL);
	tcsetattr (STDIN_FILENO, TCSANOW, &new_opts);
	c = getc (stdin);

	/* restore old settings */
	res = tcsetattr (STDIN_FILENO, TCSANOW, &org_opts);
	if (res != 0)
		g_warning ("failed to set terminal");
	return c;
}

/**
 * zif_cmd_prompt:
 **/
static gboolean
zif_cmd_prompt (const gchar *title)
{
	gchar input;
	while (TRUE) {
		g_print ("%s [y/N] ", title);

		fflush (stdin);
		input = zif_cmd_getchar_unbuffered ();
		g_print ("%c\n", input);
	
		if (input == 'y' || input == 'Y')
			return TRUE;
		else if (input == 'n' || input == 'N')
			return FALSE;
	}
}

/**
 * zif_transaction_reason_to_string_localized:
 **/
static const gchar *
zif_transaction_reason_to_string_localized (ZifTransactionReason reason)
{
	const gchar *str = NULL;
	switch (reason) {
	case ZIF_TRANSACTION_REASON_INSTALL_DEPEND:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Installing for dependencies");
		break;
	case ZIF_TRANSACTION_REASON_INSTALL_FOR_UPDATE:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Updating to new versions");
		break;
	case ZIF_TRANSACTION_REASON_INSTALL_USER_ACTION:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Installing");
		break;
	case ZIF_TRANSACTION_REASON_REMOVE_AS_ONLYN:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Removing due to multiple versions");
		break;
	case ZIF_TRANSACTION_REASON_REMOVE_FOR_DEP:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Removing for dependencies");
		break;
	case ZIF_TRANSACTION_REASON_REMOVE_FOR_UPDATE:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Removing old versions");
		break;
	case ZIF_TRANSACTION_REASON_REMOVE_OBSOLETE:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Removing as obsolete");
		break;
	case ZIF_TRANSACTION_REASON_REMOVE_USER_ACTION:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Removing");
		break;
	case ZIF_TRANSACTION_REASON_UPDATE_USER_ACTION:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Updating");
		break;
	case ZIF_TRANSACTION_REASON_UPDATE_FOR_CONFLICT:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Updating for conflict");
		break;
	case ZIF_TRANSACTION_REASON_UPDATE_DEPEND:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Updating for dependencies");
		break;
	case ZIF_TRANSACTION_REASON_UPDATE_SYSTEM:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Updating the system");
		break;
	case ZIF_TRANSACTION_REASON_DOWNGRADE_USER_ACTION:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Downgrading");
		break;
	case ZIF_TRANSACTION_REASON_DOWNGRADE_FOR_DEP:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Downgrading for dependencies");
		break;
	default:
		/* TRANSLATORS: this is the reason the action is to be taken */
		str = _("Unknown reason");
		break;
	}
	return str;
}

/* print what's going to happen */
static void
zif_main_show_transaction (ZifTransaction *transaction)
{
	gboolean has_downgrade = FALSE;
	GPtrArray *array;
	guint i, j;
	ZifPackage *package;
	gint order[] = {
		ZIF_TRANSACTION_REASON_INSTALL_USER_ACTION,
		ZIF_TRANSACTION_REASON_INSTALL_FOR_UPDATE,
		ZIF_TRANSACTION_REASON_INSTALL_DEPEND,
		ZIF_TRANSACTION_REASON_DOWNGRADE_USER_ACTION,
		ZIF_TRANSACTION_REASON_DOWNGRADE_FOR_DEP,
		ZIF_TRANSACTION_REASON_UPDATE_USER_ACTION,
		ZIF_TRANSACTION_REASON_UPDATE_SYSTEM,
		ZIF_TRANSACTION_REASON_UPDATE_DEPEND,
		ZIF_TRANSACTION_REASON_UPDATE_FOR_CONFLICT,
		ZIF_TRANSACTION_REASON_REMOVE_USER_ACTION,
		ZIF_TRANSACTION_REASON_REMOVE_FOR_DEP,
		ZIF_TRANSACTION_REASON_REMOVE_AS_ONLYN,
		ZIF_TRANSACTION_REASON_REMOVE_OBSOLETE,
		-1 };

	g_print ("%s\n", _("Transaction summary:"));
	for (i=0; order[i] != -1; i++) {
		array = zif_transaction_get_array_for_reason (transaction, order[i]);
		if (array->len > 0) {
			g_print ("  %s:\n", zif_transaction_reason_to_string_localized (order[i]));
			for (j=0; j<array->len; j++) {
				package = g_ptr_array_index (array, j);
				g_print ("  %i.\t%s\n",
					 j+1,
					 zif_package_get_printable (package));
			}
			if (order[i] == ZIF_TRANSACTION_REASON_DOWNGRADE_USER_ACTION ||
			    order[i] == ZIF_TRANSACTION_REASON_DOWNGRADE_FOR_DEP)
				has_downgrade = TRUE;
		}
		g_ptr_array_unref (array);
	}
	if (has_downgrade) {
		/* TRANSLATOR: downgrades are bad and not supported */
		g_print ("\n%s\n\n", _("WARNING: Downgrading packages is not supported or tested."));
	}
}

/**
 * zif_main_report_transaction_warnings:
 **/
static void
zif_main_report_transaction_warnings (ZifTransaction *transaction)
{
	const gchar *script_output;
	gchar **split = NULL;
	guint i;

	/* get the stderr and stdout */
	script_output = zif_transaction_get_script_output (transaction);
	if (script_output == NULL)
		goto out;

	split = g_strsplit (script_output, "\n", -1);
	for (i = 0; split[i] != NULL; i++) {

		/* skip blank lines */
		if (split[i][0] == '\0')
			continue;

		/* TRANSLATORS: this is the stdout and stderr output
		 * from the transaction, that may indicate something
		 * went wrong */
		g_print ("%s %s\n", _("Transaction warning:"), split[i]);
	}
out:
	g_strfreev (split);
}

/**
 * zif_main_get_transaction_download_size:
 **/
static guint64
zif_main_get_transaction_download_size (ZifTransaction *transaction,
					ZifState *state,
					GError **error)
{
	gboolean ret;
	GPtrArray *install;
	guint64 size;
	guint64 size_retval = G_MAXUINT64;
	guint64 size_total = 0;
	guint i;
	ZifPackage *package_tmp;
	ZifState *state_local;
	ZifState *state_loop;

	/* get the packages that are being installed */
	install = zif_transaction_get_install (transaction);
	if (install->len == 0) {
		size_retval = 0;
		goto out;
	}

	/* find each package's remote size */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, install->len);
	for (i=0; i<install->len; i++) {
		package_tmp = g_ptr_array_index (install, i);
		if (ZIF_IS_PACKAGE_REMOTE (package_tmp)) {
			state_loop = zif_state_get_child (state_local);
			size = zif_package_get_size (package_tmp,
						     state_loop,
						     error);
			if (size == 0)
				goto out;
		} else {
			size = 0;
		}
		size_total += size;

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* success */
	size_retval = size_total;
out:
	g_ptr_array_unref (install);
	return size_retval;
}

#if !GLIB_CHECK_VERSION(2,29,14)
static gchar *
g_format_size (guint64 size)
{
	return g_strdup_printf ("%.1f Mb", (gdouble) size / (1024 * 1024));
}
#endif

/**
 * zif_transaction_run:
 **/
static gboolean
zif_transaction_run (ZifCmdPrivate *priv, ZifTransaction *transaction, ZifState *state, GError **error)
{
	gboolean assume_yes;
	gboolean ret;
	gboolean untrusted = FALSE;
	gchar *size_str = NULL;
	GPtrArray *install = NULL;
	GPtrArray *store_array_remote = NULL;
	guint64 size;
	guint i;
	ZifPackage *package_tmp;
	ZifState *state_local;

	/* setup steps */
	ret = zif_state_set_steps (state,
				   error,
				   1, /* add remote stores */
				   25, /* resolve */
				   5, /* get sizes */
				   30, /* prepare */
				   39, /* commit */
				   -1);
	if (!ret)
		goto out;

	/* get remote enabled stores */
	store_array_remote = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_remote_enabled (store_array_remote, state_local, error);
	if (!ret)
		goto out;

	/* set local store */
	zif_transaction_set_store_local (transaction, priv->store_local);

	/* add remote stores */
	zif_transaction_set_stores_remote (transaction, store_array_remote);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* resolve */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_resolve (transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* show */
	zif_progress_bar_end (priv->progressbar);
	zif_main_show_transaction (transaction);

	/* confirm */
	if (priv->assume_no) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error_literal (error, 1, 0, _("Automatically declined action"));
		goto out;
	}

	/* get size */
	state_local = zif_state_get_child (state);
	size = zif_main_get_transaction_download_size (transaction,
						       state_local,
						       error);
	if (size == G_MAXUINT64) {
		ret = FALSE;
		goto out;
	}

	/* inform the user in case it's costing per megabyte */
	if (size > 0) {
		size_str = g_format_size (size);
		/* TRANSLATORS: how much we have to download */
		g_print ("%s: %s\n", _("Total download size"), size_str);
		g_free (size_str);
	}

	/* ask the question */
	assume_yes = zif_config_get_boolean (priv->config, "assumeyes", NULL);
	if (!assume_yes) {
		if (!zif_cmd_prompt (_("Run transaction?"))) {
			ret = FALSE;
			/* TRANSLATORS: error message */
			g_set_error_literal (error, 1, 0, _("User declined action"));
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* prepare */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_prepare (transaction, state_local, error);
	if (!ret)
		goto out;

	/* confirm we want unsigned packages */
	install = zif_transaction_get_install (transaction);
	for (i=0; i<install->len; i++) {
		package_tmp = g_ptr_array_index (install, i);
		if (zif_package_get_trust_kind (package_tmp) != ZIF_PACKAGE_TRUST_KIND_PUBKEY) {
			untrusted = TRUE;
			break;
		}
	}
	if (untrusted) {
		/* TRANSLATORS: untrusted packages might be dangerous */
		zif_progress_bar_end (priv->progressbar);
		g_print ("%s\n", _("There are untrusted packages:"));
		zif_print_packages (install);
	}
	if (untrusted && !assume_yes) {
		if (!zif_cmd_prompt (_("Run transaction?"))) {
			ret = FALSE;
			/* TRANSLATORS: error message */
			g_set_error_literal (error, 1, 0, _("User declined action"));
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* commit */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_commit_full (transaction,
					   ZIF_TRANSACTION_FLAG_ALLOW_UNTRUSTED,
					   state_local,
					   error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* print the output of the transaction, if any */
	zif_main_report_transaction_warnings (transaction);

	/* TRANSLATORS: tell the user everything went okay */
	g_print ("%s\n", _("Transaction success!"));
out:
	if (store_array_remote != NULL)
		g_ptr_array_unref (store_array_remote);
	if (install != NULL)
		g_ptr_array_unref (install);
	return ret;
}

/**
 * zif_cmd_install:
 **/
static gboolean
zif_cmd_install (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean enable_debuginfo;
	gboolean has_debuginfo = FALSE;
	gboolean ret = FALSE;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *store_array_local = NULL;
	GPtrArray *store_array_remote = NULL;
	GPtrArray *store_array_all = NULL;
	guint i;
	ZifPackage *package;
	ZifRepos *repos = NULL;
	ZifState *state_local;
	ZifStore *store_tmp;
	ZifTransaction *transaction = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: A user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a package name"));
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Installing"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   5, /* resolve */
				   1, /* add remote */
				   1, /* possibly enable -debuginfo */
				   12, /* find remote */
				   80, /* run transaction */
				   -1);
	if (!ret)
		goto out;

	/* add all stores */
	store_array_local = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array_local, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check not already installed */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array_local,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB |
					      ZIF_STORE_RESOLVE_FLAG_PREFER_NATIVE,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len > 0) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, _("%s is already installed"), values[0]);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* are there any debuginfo packages */
	for (i=0; values[i] != NULL; i++) {
		if (g_str_has_suffix (values[i], "-debuginfo")) {
			has_debuginfo = TRUE;
			break;
		}
	}

	/* check available */
	store_array_remote = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array_remote, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* the yum-plugin-auto-update-debug-info code "helpfully"
	 * enables debuginfo repos when we try to install -debuginfo
	 * packages, so we should probably copy that behaviour */
	enable_debuginfo = zif_config_get_boolean (priv->config,
						   "auto_enable_debuginfo",
						   NULL);
	if (enable_debuginfo && has_debuginfo) {
		repos = zif_repos_new ();

		/* enable any repos that have suffix -debuginfo */
		state_local = zif_state_get_child (priv->state);
		store_array_all = zif_repos_get_stores (repos, state_local, error);
		if (store_array_all == NULL)
			goto out;
		for (i=0; i<store_array_all->len; i++) {
			store_tmp = g_ptr_array_index (store_array_all, i);
			if (g_str_has_suffix (zif_store_get_id (store_tmp),
					      "-debuginfo")) {
				zif_store_array_add_store (store_array_remote,
							   store_tmp);
			}
		}

		/* force this on, as some source repos don't provide the
		 * right distro version */
		ret = zif_config_unset (priv->config,
					"skip_broken",
					error);
		if (!ret)
			goto out;
		ret = zif_config_set_boolean (priv->config,
					      "skip_broken",
					      TRUE,
					      error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check we can find a package of this name */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array_remote,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB |
					      ZIF_STORE_RESOLVE_FLAG_PREFER_NATIVE,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("%i Prefilter %s", i, zif_package_get_printable (package));
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("%i Postfilter %s", i, zif_package_get_printable (package));
	}
	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* install these packages */
	transaction = zif_transaction_new ();
	zif_transaction_set_euid (transaction, priv->uid);
	zif_transaction_set_cmdline (transaction, priv->cmdline);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("Adding %s", zif_package_get_printable (package));
		ret = zif_transaction_add_install (transaction, package, &error_local);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to add install %s: %s",
				 zif_package_get_name (package),
				 error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* are we running super verbose? */
	zif_transaction_set_verbose (transaction,
				     g_getenv ("ZIF_DEPSOLVE_DEBUG") != NULL);

	/* run what we've got */
	state_local = zif_state_get_child (priv->state);
	ret = zif_transaction_run (priv, transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* success */
	ret = TRUE;
out:
	if (store_array_all != NULL)
		g_ptr_array_unref (store_array_all);
	if (repos != NULL)
		g_object_unref (repos);
	if (transaction != NULL)
		g_object_unref (transaction);
	if (store_array_local != NULL)
		g_ptr_array_unref (store_array_local);
	if (store_array_remote != NULL)
		g_ptr_array_unref (store_array_remote);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_downgrade:
 **/
static gboolean
zif_cmd_downgrade (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *store_array_local = NULL;
	GPtrArray *store_array_remote = NULL;
	guint i;
	ZifPackage *package;
	ZifPackage *package_installed = NULL;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: A user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a package name"));
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Downgrading"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   5, /* resolve */
				   1, /* add remote */
				   13, /* find remote */
				   80, /* run transaction */
				   -1);
	if (!ret)
		goto out;

	/* add all stores */
	store_array_local = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array_local, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check already installed */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array_local,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB |
					      ZIF_STORE_RESOLVE_FLAG_PREFER_NATIVE,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, _("%s is not already installed"), values[0]);
		goto out;
	}
	package_installed = g_object_ref (g_ptr_array_index (array, 0));

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check available */
	store_array_remote = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array_remote, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check we can find a package of this name */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array_remote,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB |
					      ZIF_STORE_RESOLVE_FLAG_PREFER_NATIVE,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("%i Prefilter %s", i,
			 zif_package_get_printable (package));
	}

	/* remove any packages with the installed version */
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		if (g_strcmp0 (zif_package_get_version (package),
			       zif_package_get_version (package_installed)) == 0) {
			g_ptr_array_remove_index (array, i);
		} else {
			i++;
		}
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("%i Postfilter %s", i,
			 zif_package_get_printable (package));
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* install these packages */
	transaction = zif_transaction_new ();
	zif_transaction_set_euid (transaction, priv->uid);
	zif_transaction_set_cmdline (transaction, priv->cmdline);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("Adding %s", zif_package_get_printable (package));
		ret = zif_transaction_add_install_as_downgrade (transaction,
								package,
								&error_local);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to add install %s: %s",
				 zif_package_get_name (package),
				 error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* are we running super verbose? */
	zif_transaction_set_verbose (transaction,
				     g_getenv ("ZIF_DEPSOLVE_DEBUG") != NULL);

	/* run what we've got */
	state_local = zif_state_get_child (priv->state);
	ret = zif_transaction_run (priv, transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* success */
	ret = TRUE;
out:
	if (package_installed != NULL)
		g_object_unref (package_installed);
	if (transaction != NULL)
		g_object_unref (transaction);
	if (store_array_local != NULL)
		g_ptr_array_unref (store_array_local);
	if (store_array_remote != NULL)
		g_ptr_array_unref (store_array_remote);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_local_install:
 **/
static gboolean
zif_cmd_local_install (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	guint i;
	ZifPackage *package = NULL;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a filename");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Installing file"));
	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* read local files */
				   49, /* add install to transaction */
				   50, /* run transaction */
				   -1);
	if (!ret)
		goto out;

	/* read file */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; values[i] != NULL; i++) {
		package = zif_package_local_new ();
		g_ptr_array_add (array, package);
		ret = zif_package_local_set_from_filename (ZIF_PACKAGE_LOCAL (package),
							   values[i],
							   error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* add the files to the transaction */
	transaction = zif_transaction_new ();
	zif_transaction_set_euid (transaction, priv->uid);
	zif_transaction_set_cmdline (transaction, priv->cmdline);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_transaction_add_install (transaction, package, error);
		if (!ret)
			goto out;
	}

	/* are we running super verbose? */
	zif_transaction_set_verbose (transaction,
				     g_getenv ("ZIF_DEPSOLVE_DEBUG") != NULL);

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* run what we've got */
	state_local = zif_state_get_child (priv->state);
	ret = zif_transaction_run (priv, transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* success */
	ret = TRUE;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (transaction != NULL)
		g_object_unref (transaction);
	return ret;
}

/**
 * zif_cmd_build_depends:
 **/
static gboolean
zif_cmd_build_depends (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean is_source;
	gboolean ret;
	gchar *archinfo = NULL;
	GError *error_local = NULL;
	GPtrArray *depends_all = NULL;
	GPtrArray *depends = NULL;
	GPtrArray *packages = NULL;
	GPtrArray *provides = NULL;
	GPtrArray *stores_enabled = NULL;
	GPtrArray *stores = NULL;
	guint i;
	guint j;
	ZifDepend *depend;
	ZifPackage *package;
	ZifRepos *repos = NULL;
	ZifState *state_local;
	ZifState *state_loop;
	ZifStore *store;
	ZifTransaction *transaction = NULL;

	/* force this on, as some source repos don't provide the
	 * right distro version */
	ret = zif_config_unset (priv->config,
				"skip_broken",
				error);
	if (!ret)
		goto out;
	ret = zif_config_set_boolean (priv->config,
				      "skip_broken",
				      TRUE,
				      error);
	if (!ret)
		goto out;

	/* ZifRepos */
	repos = zif_repos_new ();

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   20, /* get repos */
				   5, /* get enabled repos */
				   5, /* resolve */
				   20, /* get-requires */
				   25, /* what-provides */
				   25, /* do install */
				   -1);
	if (!ret)
		goto out;

	/* get all repos */
	state_local = zif_state_get_child (priv->state);
	stores = zif_repos_get_stores (repos,
				       state_local,
				       error);
	if (stores == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* get all enabled repos */
	state_local = zif_state_get_child (priv->state);
	stores_enabled = zif_repos_get_stores_enabled (repos,
						       state_local,
						       error);
	if (stores_enabled == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* runtime enable the -source ones and disable others */
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);
		is_source = g_str_has_suffix (zif_store_get_id (store),
					      "-source");
		g_debug ("is source %s: %i",
			 zif_store_get_id (store),
			 is_source);
		zif_store_set_enabled (store, is_source);
	}

	/* resolve for the set of packages */
	state_local = zif_state_get_child (priv->state);
	packages = zif_store_array_resolve_full (stores,
						 values,
						 ZIF_STORE_RESOLVE_FLAG_USE_ALL |
						 ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
						 state_local,
						 error);
	if (packages == NULL) {
		ret = FALSE;
		goto out;
	}

	/* found nothing */
	if (packages->len == 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "no packages found");
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* get depends on each package */
	depends_all = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, packages->len);
	for (i=0; i<packages->len; i++) {
		package = g_ptr_array_index (packages, i);

		/* get required for each source package */
		state_loop = zif_state_get_child (state_local);
		depends = zif_package_get_requires (package,
						    state_loop,
						    error);
		if (depends == NULL)
			goto out;
		for (j=0; j<depends->len; j++) {
			depend = g_ptr_array_index (depends, j);
			g_debug ("%s needs %s",
				 zif_package_get_printable (package),
				 zif_depend_get_description (depend));
			g_ptr_array_add (depends_all, g_object_ref (depend));
		}
		g_ptr_array_unref (depends);

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;

	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* runtime enable the normal repos */
	for (i=0; i<stores_enabled->len; i++) {
		store = g_ptr_array_index (stores_enabled, i);
		zif_store_set_enabled (store, TRUE);
	}

	/* get packages providing this dep */
	state_local = zif_state_get_child (priv->state);
	provides = zif_store_array_what_provides (stores_enabled,
						  depends_all,
						  state_local,
						  error);
	for (i=0; i<provides->len; i++) {
		package = g_ptr_array_index (provides, i);
		g_debug ("require installed %s",
			 zif_package_get_printable (package));
	}

	/* filter to something sane */
	zif_package_array_filter_duplicates (provides);
	zif_package_array_filter_newest (provides);
	archinfo = zif_config_get_string (priv->config,
					  "archinfo", NULL);
	zif_package_array_filter_best_arch (provides, archinfo);

	for (i=0; i<provides->len; i++) {
		package = g_ptr_array_index (provides, i);
		g_debug ("post filter installed %s",
			 zif_package_get_printable (package));
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* install these non-installed packages */
	transaction = zif_transaction_new ();
	for (i=0; i<provides->len; i++) {
		package = g_ptr_array_index (provides, i);
		g_debug ("Adding %s", zif_package_get_printable (package));
		ret = zif_transaction_add_install (transaction,
						   package,
						   &error_local);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to add install %s: %s",
				     zif_package_get_name (package),
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* are we running super verbose? */
	zif_transaction_set_verbose (transaction,
				     g_getenv ("ZIF_DEPSOLVE_DEBUG") != NULL);

	/* run what we've got */
	state_local = zif_state_get_child (priv->state);
	ret = zif_transaction_run (priv, transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
out:
	g_free (archinfo);
	g_object_unref (repos);
	if (transaction != NULL)
		g_object_unref (transaction);
	if (depends_all != NULL)
		g_ptr_array_unref (depends_all);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	if (provides != NULL)
		g_ptr_array_unref (provides);
	if (stores != NULL)
		g_ptr_array_unref (stores);
	if (stores_enabled != NULL)
		g_ptr_array_unref (stores_enabled);
	return ret;
}

/**
 * zif_cmd_manifest_check:
 **/
static gboolean
zif_cmd_manifest_check (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	guint i;
	ZifManifest *manifest = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a filename");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Checking manifests"));

	/* setup state */
	zif_state_set_number_steps (priv->state, g_strv_length (values));

	/* check the manifest */
	manifest = zif_manifest_new ();
	for (i=0; values[i] != NULL; i++) {
		state_local = zif_state_get_child (priv->state);
		ret = zif_manifest_check (manifest, values[i], state_local, error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (priv->state, error);
		if (!ret)
			goto out;
	}

	/* success */
	zif_progress_bar_end (priv->progressbar);
	g_print ("%s\n", _("All manifest files were checked successfully"));

	/* success */
	ret = TRUE;
out:
	if (manifest != NULL)
		g_object_unref (manifest);
	return ret;
}

/**
 * zif_cmd_manifest_dump:
 **/
static gboolean
zif_cmd_manifest_dump (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array_local = NULL;
	GPtrArray *array_remote = NULL;
	GPtrArray *store_array = NULL;
	GString *string = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a filename");
		goto out;
	}

	if (!g_str_has_suffix (values[0], ".manifest")) {
		g_set_error (error, 1, 0, "%s does not end in manifest", values[0]);
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Dumping manifest"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   1, /* add remote */
				   90, /* get local packages */
				   6, /* get remote packages */
				   2, /* save */
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	array_remote = zif_store_array_get_packages (store_array, state_local, error);
	if (array_remote == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	array_local = zif_store_get_packages (priv->store_local, state_local, error);
	if (array_local == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* save to file */
	string = g_string_new ("# automatically generated manifest\n");
	g_string_append (string, "local\n");
	for (i=0; i<array_local->len; i++) {
		package = g_ptr_array_index (array_local, i);
		g_string_append_printf (string, "\t%s\n", zif_package_get_id (package));
	}
	g_string_set_size (string, string->len - 1);
	g_string_append (string, "\n\n");
	g_string_append (string, "remote\n");
	for (i=0; i<array_remote->len; i++) {
		package = g_ptr_array_index (array_remote, i);
		g_string_append_printf (string, "\t%s\n", zif_package_get_id (package));
	}
	g_string_set_size (string, string->len - 1);
	g_string_append (string, "\n");

	/* save */
	ret = g_file_set_contents (values[0], string->str, string->len, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array_local != NULL)
		g_ptr_array_unref (array_local);
	if (array_remote != NULL)
		g_ptr_array_unref (array_remote);
	if (string != NULL)
		g_string_free (string, TRUE);
	return ret;
}

/**
 * zif_cmd_refresh_cache:
 **/
static gboolean
zif_cmd_refresh_cache (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	GPtrArray *store_array = NULL;
	ZifState *state_local;

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Refreshing cache"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   50,
				   50,
				   -1);
	if (!ret)
		goto out;

	/* add remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* refresh all ZifRemoteStores */
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_refresh (store_array,
				       (values[0] != NULL),
				       state_local,
				       error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* done */
	zif_progress_bar_end (priv->progressbar);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return ret;
}

/**
 * zif_cmd_remove:
 **/
static gboolean
zif_cmd_remove (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array_local = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: A user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a package name"));
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Removing"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   30, /* resolve */
				   69, /* transaction run */
				   -1);
	if (!ret)
		goto out;

	/* add local store */
	store_array_local = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array_local, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check not already installed */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array_local,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, _("The package is not installed"));
		goto out;
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* remove these packages */
	transaction = zif_transaction_new ();
	zif_transaction_set_euid (transaction, priv->uid);
	zif_transaction_set_cmdline (transaction, priv->cmdline);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_transaction_add_remove (transaction, package, error);
		if (!ret)
			goto out;
	}

	/* are we running super verbose? */
	zif_transaction_set_verbose (transaction,
				     g_getenv ("ZIF_DEPSOLVE_DEBUG") != NULL);

	/* run what we've got */
	state_local = zif_state_get_child (priv->state);
	ret = zif_transaction_run (priv, transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	g_ptr_array_unref (store_array_local);

	zif_progress_bar_end (priv->progressbar);

	/* success */
	ret = TRUE;
out:
	if (transaction != NULL)
		g_object_unref (transaction);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_repo_disable:
 **/
static gboolean
zif_cmd_repo_disable (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	ZifState *state_local;
	ZifStoreRemote *store_remote = NULL;
	ZifRepos *repos;

	/* ZifRepos */
	repos = zif_repos_new ();

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a repo name");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Disabling repo"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   50,
				   50,
				   -1);
	if (!ret)
		goto out;

	/* get repo */
	state_local = zif_state_get_child (priv->state);
	store_remote = zif_repos_get_store (repos, values[0], state_local, error);
	if (store_remote == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* change the enabled state */
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_remote_set_enabled (store_remote, FALSE, state_local, error);
	if (!ret) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* success */
	ret = TRUE;
out:
	g_object_unref (repos);
	if (store_remote)
		g_object_unref (store_remote);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_repo_enable:
 **/
static gboolean
zif_cmd_repo_enable (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	ZifRepos *repos;
	ZifState *state_local;
	ZifStoreRemote *store_remote = NULL;

	/* ZifRepos */
	repos = zif_repos_new ();

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a repo name");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Enabling repo"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   50,
				   50,
				   -1);
	if (!ret)
		goto out;

	/* get repo */
	state_local = zif_state_get_child (priv->state);
	store_remote = zif_repos_get_store (repos, values[0], state_local, error);
	if (store_remote == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* change the enabled state */
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_remote_set_enabled (store_remote, TRUE, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
out:
	g_object_unref (repos);
	if (store_remote)
		g_object_unref (store_remote);
	return ret;
}

/**
 * zif_cmd_repo_list:
 **/
static gboolean
zif_cmd_repo_list (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gchar *id_pad;
	GPtrArray *array;
	GString *string = NULL;
	guint i;
	guint length;
	guint max_length = 0;
	ZifRepos *repos;
	ZifStore *store_tmp;
	gboolean enabled;
	const gchar *name;

	/* ZifRepos */
	repos = zif_repos_new ();

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Getting repo list"));

	/* get list */
	array = zif_repos_get_stores (repos, priv->state, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get maximum id string length */
	for (i=0; i<array->len; i++) {
		store_tmp = g_ptr_array_index (array, i);
		/* ITS4: ignore, only used for formatting */
		length = strlen (zif_store_get_id (store_tmp));
		if (length > max_length)
			max_length = length;
	}

	/* populate string */
	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		store_tmp = g_ptr_array_index (array, i);
		id_pad = zif_strpad (zif_store_get_id (store_tmp), max_length);

		/* get data */
		zif_state_reset (priv->state);
		name =  zif_store_remote_get_name (ZIF_STORE_REMOTE (store_tmp),
						   priv->state, error);
		if (name == NULL) {
			ret = FALSE;
			goto out;
		}
		zif_state_reset (priv->state);
		enabled = zif_store_remote_get_enabled (ZIF_STORE_REMOTE (store_tmp),
							priv->state, NULL);
		g_string_append (string, id_pad);
		g_string_append (string, enabled ? " enabled   " : " disabled  ");
		g_string_append (string, name);
		g_string_append (string, "\n");
		g_free (id_pad);
	}

	zif_progress_bar_end (priv->progressbar);

	/* print */
	g_print ("%s", string->str);

	/* success */
	ret = TRUE;
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	g_object_unref (repos);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_resolve:
 **/
static gboolean
zif_cmd_resolve (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: A user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a package name"));
		goto out;
	}

	/* TRANSLATORS: finding packages from a name */
	zif_progress_bar_start (priv->progressbar, _("Finding package"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   1, /* add remote */
				   98, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_PREFER_NATIVE |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_search_category:
 **/
static gboolean
zif_cmd_search_category (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: A user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a category"));
		goto out;
	}

	/* TRANSLATORS: returning all packages that match a category */
	zif_progress_bar_start (priv->progressbar, _("Search category"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   2, /* add remote */
				   98, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_search_category (store_array, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_package_array_filter_newest (array);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_search_details:
 **/
static gboolean
zif_cmd_search_details (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: user needs to specify something */
		g_set_error (error, 1, 0, _("No search term specified"));
		goto out;
	}

	/* TRANSLATORS: searching by package details, not just name */
	zif_progress_bar_start (priv->progressbar, _("Searching details"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   1, /* add remote */
				   98, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add local packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* add remote packages */
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_search_details (store_array, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_package_array_filter_newest (array);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_search_file:
 **/
static gboolean
zif_cmd_search_file (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	ZifState *state_local;

	/* no files */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: user needs to specify something */
		g_set_error_literal (error, 1, 0, _("Specify a filename"));
		goto out;
	}

	/* TRANSLATORS: searching for a specific file */
	zif_progress_bar_start (priv->progressbar, _("Searching file"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   1, /* add remote */
				   98, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add local packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* add remote packages */
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* search file */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_search_file (store_array, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_package_array_filter_newest (array);
	zif_print_packages (array);

	/* success */
	ret = TRUE;
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_search_group:
 **/
static gboolean
zif_cmd_search_group (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: user needs to specify something */
		g_set_error (error, 1, 0, _("No search term specified"));
		goto out;
	}

	/* TRANSLATORS: searching by a specific group */
	zif_progress_bar_start (priv->progressbar, _("Search group"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   1, /* add remote */
				   98, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_search_group (store_array, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_search_name:
 **/
static gboolean
zif_cmd_search_name (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: user needs to specify something */
		g_set_error (error, 1, 0, _("No search term specified"));
		goto out;
	}

	/* TRANSLATORS: search, based on the package name only */
	zif_progress_bar_start (priv->progressbar, _("Searching name"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   1, /* add remote */
				   98, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_search_name (store_array, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_package_array_filter_newest (array);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_update_all:
 **/
static gboolean
zif_cmd_update_all (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* TRANSLATORS: used when the user did not explicitly specify a
	 * list of updates to install */
	zif_progress_bar_start (priv->progressbar, _("Updating system"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   10, /* get list of updates */
				   5, /* add updates */
				   85, /* run transaction */
				   -1);
	if (!ret)
		goto out;

	/* get updates array */
	state_local = zif_state_get_child (priv->state);
	array = zif_get_update_array (priv, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* update these packages */
	transaction = zif_transaction_new ();
	zif_transaction_set_euid (transaction, priv->uid);
	zif_transaction_set_cmdline (transaction, priv->cmdline);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_transaction_add_install_as_update (transaction, package, error);
		if (!ret)
			goto out;
	}

	/* are we running super verbose? */
	zif_transaction_set_verbose (transaction,
				     g_getenv ("ZIF_DEPSOLVE_DEBUG") != NULL);

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* run what we've got */
	state_local = zif_state_get_child (priv->state);
	ret = zif_transaction_run (priv, transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
out:
	if (transaction != NULL)
		g_object_unref (transaction);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_upgrade_distro_live:
 **/
static gboolean
zif_cmd_upgrade_distro_live (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	gchar **distro_id_split = NULL;
	guint version;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message, missing value */
		g_set_error (error, 1, 0,
			     "%s 'fedora-9'\n",
			     _("Specify a distro name, e.g."));
		goto out;
	}

	/* TRANSLATORS: upgrading to a new distro release, *not*
	 * updating to a new package version */
	zif_progress_bar_start (priv->progressbar, _("Upgrading"));

	/* check valid */
	distro_id_split = g_strsplit (values[0], "-", -1);
	if (g_strv_length (distro_id_split) != 2) {
		/* TRANSLATORS: error message, invalid value */
		g_set_error_literal (error, 1, 0, _("Distribution name invalid"));
		goto out;
	}

	/* check fedora */
	if (g_strcmp0 (distro_id_split[0], "fedora") != 0) {
		/* TRANSLATORS: error message, invalid value */
		g_set_error_literal (error, 1, 0, _("Only 'fedora' is supported"));
		goto out;
	}

	/* check release */
	version = atoi (distro_id_split[1]);
	if (version < 13 || version > 99) {
		/* TRANSLATORS: error message, invalid value */
		g_set_error (error, 1, 0, _("Version number %i is invalid"), version);
		goto out;
	}

	/* change the releasever */
	ret = zif_config_unset (priv->config,
				"releasever",
				error);
	if (!ret)
		goto out;
	ret = zif_config_set_uint (priv->config,
				   "releasever",
				   version,
				   error);
	if (!ret)
		goto out;

	/* set the compare mode */
	ret = zif_config_set_string (priv->config,
				     "pkg_compare_mode",
				     "distro",
				     error);
	if (!ret)
		goto out;

	/* do the live upgrade */
	ret = zif_cmd_update_all (priv, values, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
out:
	g_strfreev (distro_id_split);
	return ret;
}

/**
 * zif_cmd_update:
 **/
static gboolean
zif_cmd_update (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		ret = zif_cmd_update_all (priv, values, error);
		goto out;
	}

	/* TRANSLATORS: updating several packages */
	zif_progress_bar_start (priv->progressbar, _("Updating"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   5, /* add updates */
				   94, /* run transaction */
				   -1);
	if (!ret)
		goto out;

	/* add local store */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check not already installed */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, _("The %s package is not installed"), values[0]);
		goto out;
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* update this package */
	transaction = zif_transaction_new ();
	zif_transaction_set_euid (transaction, priv->uid);
	zif_transaction_set_cmdline (transaction, priv->cmdline);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_transaction_add_update (transaction, package, error);
		if (!ret)
			goto out;
	}

	/* are we running super verbose? */
	zif_transaction_set_verbose (transaction,
				     g_getenv ("ZIF_DEPSOLVE_DEBUG") != NULL);

	/* run what we've got */
	state_local = zif_state_get_child (priv->state);
	ret = zif_transaction_run (priv, transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* success */
	ret = TRUE;
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (transaction != NULL)
		g_object_unref (transaction);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_distro_sync:
 **/
static gboolean
zif_cmd_distro_sync (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ret = zif_config_set_string (priv->config,
				     "pkg_compare_mode",
				     "distro",
				     error);
	if (!ret)
		goto out;
	ret = zif_cmd_update (priv, values, error);
out:
	return ret;
}

/**
 * zif_cmd_update_details:
 **/
static gboolean
zif_cmd_update_details (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *changelog;
	GPtrArray *store_array_local = NULL;
	GPtrArray *store_array = NULL;
	GPtrArray *update_infos;
	GString *string = NULL;
	guint i;
	guint j;
	ZifChangeset *changeset;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;
	ZifUpdateInfo *info;
	ZifUpdate *update;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		ret = zif_cmd_update_all (priv, values, error);
		goto out;
	}

	/* TRANSLATORS: gettin details about an update */
	zif_progress_bar_start (priv->progressbar, _("Getting details"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add remote */
				   5, /* resolve */
				   94, /* get data */
				   -1);
	if (!ret)
		goto out;

	/* add local store */
	store_array_local = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array_local, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check not already installed */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve_full (store_array_local,
					      values,
					      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
					      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
					      state_local,
					      error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* filter the results in a sane way */
	ret = zif_filter_post_resolve (priv, array, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* get update details */
	string = g_string_new ("");
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, array->len);
	for (i=0; i<array->len; i++) {

		package = g_ptr_array_index (array, i);
		g_string_append_printf (string, "%s:\n",
					zif_package_get_printable (package));

		state_loop = zif_state_get_child (state_local);
		update = zif_package_remote_get_update_detail (ZIF_PACKAGE_REMOTE (package),
							       state_loop,
							       &error_local);
		if (update == NULL) {

			/* are we trying to get updatinfo data from a
			 * repo without any support? */
			if (error_local->domain == ZIF_PACKAGE_ERROR &&
			    error_local->code == ZIF_PACKAGE_ERROR_NO_SUPPORT) {
				g_debug ("ignoring error: %s",
					 error_local->message);
				g_clear_error (&error_local);

				/* these sections done */
				ret = zif_state_finished (state_loop, error);
				if (!ret)
					goto out;
				ret = zif_state_done (state_local, error);
				if (!ret)
					goto out;

				/* TRANSLATORS: when a package has no update details */
				g_string_append_printf (string, " - %s\n\n",
							_("No update detail"));

				/* carry on as if nothing had happened */
				continue;
			}
			ret = FALSE;
			g_set_error (error, 1, 0, "failed to get update detail for %s: %s",
				 zif_package_get_printable (package), error_local->message);
			g_clear_error (&error_local);
			goto out;
		}

		/* TRANSLATORS: these are headings for the update details */
		if (zif_update_get_kind (update) != ZIF_UPDATE_KIND_UNKNOWN) {
			g_string_append_printf (string, "%s\t%s\n",
						_("Kind:"), zif_update_kind_to_string (zif_update_get_kind (update)));
		}
		if (zif_update_get_state (update) != ZIF_UPDATE_STATE_UNKNOWN) {
			g_string_append_printf (string, "%s\t%s\n",
						_("State:"), zif_update_state_to_string (zif_update_get_state (update)));
		}
		if (zif_update_get_id (update) != NULL) {
			g_string_append_printf (string, "%s\t%s\n",
						_("ID:"), zif_update_get_id (update));
		}
		if (zif_update_get_title (update) != NULL) {
			g_string_append_printf (string, "%s\t%s\n",
						_("Title:"), zif_update_get_title (update));
		}
		if (zif_update_get_description (update) != NULL) {
			g_string_append_printf (string, "%s\t%s\n",
						_("Description:"), zif_update_get_description (update));
		}
		if (zif_update_get_issued (update) != NULL) {
			g_string_append_printf (string, "%s\t%s\n",
						_("Issued:"), zif_update_get_issued (update));
		}
		update_infos = zif_update_get_update_infos (update);
		for (j=0; j<update_infos->len; j++) {
			info = g_ptr_array_index (update_infos, j);
			g_string_append_printf (string, "%s\t%s\t%s\n",
						zif_update_info_kind_to_string (zif_update_info_get_kind (info)),
						zif_update_info_get_url (info),
						zif_update_info_get_title (info));
		}
		changelog = zif_update_get_changelog (update);
		for (j=0; j<changelog->len; j++) {
			changeset = g_ptr_array_index (changelog, j);
			g_string_append_printf (string, "%s - %s\n%s\n",
						zif_changeset_get_author (changeset),
						zif_changeset_get_version (changeset),
						zif_changeset_get_description (changeset));
		}

		g_string_append (string, "\n");

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* print formatted string */
	g_print ("%s", string->str);

	/* success */
	ret = TRUE;
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_upgrade:
 **/
static gboolean
zif_cmd_upgrade (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	gchar **distro_id_split = NULL;
	guint version;
	ZifRelease *release = NULL;
	ZifReleaseUpgradeKind upgrade_kind = ZIF_RELEASE_UPGRADE_KIND_LAST;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message, missing value */
		g_set_error (error, 1, 0,
			     "%s 'fedora-9'\n",
			     _("Specify a distro name, e.g."));
		goto out;
	}
	if (values[1] == NULL) {
		/* TRANSLATORS: error message, missing value */
		g_set_error (error, 1, 0,
			     "%s 'minimal', 'default' or 'complete'\n",
			     _("Specify a update type, e.g."));
		goto out;
	}
	if (g_strcmp0 (values[1], "minimal") == 0)
		upgrade_kind = ZIF_RELEASE_UPGRADE_KIND_MINIMAL;
	else if (g_strcmp0 (values[1], "default") == 0)
		upgrade_kind = ZIF_RELEASE_UPGRADE_KIND_DEFAULT;
	else if (g_strcmp0 (values[1], "complete") == 0)
		upgrade_kind = ZIF_RELEASE_UPGRADE_KIND_COMPLETE;
	if (upgrade_kind == ZIF_RELEASE_UPGRADE_KIND_LAST) {
		/* TRANSLATORS: error message, missing value */
		g_set_error (error, 1, 0,
			     "%s minimal,default,complete\n",
			     _("Invalid update type, only these types are supported:"));
		goto out;
	}

	/* TRANSLATORS: upgrading to a new distro release, *not*
	 * updating to a new package version */
	zif_progress_bar_start (priv->progressbar, _("Upgrading"));

	/* check valid */
	distro_id_split = g_strsplit (values[0], "-", -1);
	if (g_strv_length (distro_id_split) != 2) {
		/* TRANSLATORS: error message, invalid value */
		g_set_error_literal (error, 1, 0, _("Distribution name invalid"));
		goto out;
	}

	/* check fedora */
	if (g_strcmp0 (distro_id_split[0], "fedora") != 0) {
		/* TRANSLATORS: error message, invalid value */
		g_set_error_literal (error, 1, 0, _("Only 'fedora' is supported"));
		goto out;
	}

	/* check release */
	version = atoi (distro_id_split[1]);
	if (version < 13 || version > 99) {
		/* TRANSLATORS: error message, invalid value */
		g_set_error (error, 1, 0, _("Version number %i is invalid"), version);
		goto out;
	}

	/* do the upgrade */
	release = zif_release_new ();
	ret = zif_release_upgrade_version (release, version,
					   upgrade_kind,
					   priv->state,
					   error);
	if (!ret)
		goto out;

	/* clean up after ourselves */
//	g_unlink ("/boot/upgrade/vmlinuz");
//	g_unlink ("/boot/upgrade/initrd.img");

	zif_progress_bar_end (priv->progressbar);

out:
	if (release != NULL)
		g_object_unref (release);
	g_strfreev (distro_id_split);
	return ret;
}

/**
 * zif_cmd_parse_depends:
 **/
static GPtrArray *
zif_cmd_parse_depends (gchar **values, GError **error)
{
	gboolean ret;
	GPtrArray *depend_array = NULL;
	GPtrArray *depend_array_tmp = NULL;
	guint i;
	ZifDepend *depend;

	/* parse the depends */
	depend_array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; values[i] != NULL; i++) {
		depend = zif_depend_new ();
		ret = zif_depend_parse_description (depend, values[i], error);
		if (!ret) {
			g_object_unref (depend);
			goto out;
		}
		g_ptr_array_add (depend_array_tmp, depend);
	}

	/* success */
	depend_array = g_ptr_array_ref (depend_array_tmp);
out:
	g_ptr_array_unref (depend_array_tmp);
	return depend_array;
}

/**
 * zif_cmd_what_conflicts:
 **/
static gboolean
zif_cmd_what_conflicts (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	GPtrArray *depend_array = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: user needs to specify something */
		g_set_error (error, 1, 0, _("No search term specified"));
		goto out;
	}

	/* TRANSLATORS: find out what package conflicts */
	zif_progress_bar_start (priv->progressbar, _("Conflicts"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   2, /* add local */
				   3, /* add remote */
				   95, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* parse the depend */
	depend_array = zif_cmd_parse_depends (values, error);
	if (depend_array == NULL) {
		ret = FALSE;
		goto out;
	}
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_what_conflicts (store_array, depend_array, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_what_obsoletes:
 **/
static gboolean
zif_cmd_what_obsoletes (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	GPtrArray *depend_array = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: user needs to specify something */
		g_set_error (error, 1, 0, _("No search term specified"));
		goto out;
	}

	/* TRANSLATORS: find out what package obsoletes */
	zif_progress_bar_start (priv->progressbar, _("Obsoletes"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   2, /* add local */
				   3, /* add remote */
				   95, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* parse the depend */
	depend_array = zif_cmd_parse_depends (values, error);
	if (depend_array == NULL) {
		ret = FALSE;
		goto out;
	}
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_what_obsoletes (store_array, depend_array, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_what_provides:
 **/
static gboolean
zif_cmd_what_provides (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	GPtrArray *depend_array = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: user needs to specify something */
		g_set_error (error, 1, 0, _("No search term specified"));
		goto out;
	}

	/* TRANSLATORS: find out what package provides */
	zif_progress_bar_start (priv->progressbar, _("Provides"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   2, /* add local */
				   3, /* add remote */
				   95, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* parse the depend */
	depend_array = zif_cmd_parse_depends (values, error);
	if (depend_array == NULL) {
		ret = FALSE;
		goto out;
	}
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_what_provides (store_array,
					       depend_array,
					       state_local,
					       error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_what_requires:
 **/
static gboolean
zif_cmd_what_requires (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	GPtrArray *depend_array = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: user needs to specify something */
		g_set_error (error, 1, 0, _("No search term specified"));
		goto out;
	}

	/* TRANSLATORS: find out what package requires */
	zif_progress_bar_start (priv->progressbar, _("Requires"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   2, /* add local */
				   3, /* add remote */
				   95, /* search */
				   -1);
	if (!ret)
		goto out;

	/* add both local and remote packages */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array,
					 state_local,
					 error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* parse the depend */
	depend_array = zif_cmd_parse_depends (values, error);
	if (depend_array == NULL) {
		ret = FALSE;
		goto out;
	}
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_what_requires (store_array,
					       depend_array,
					       state_local,
					       error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_run:
 **/
static gboolean
zif_cmd_run (ZifCmdPrivate *priv, const gchar *command, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	guint i;
	ZifCmdItem *item;
	GString *string;

	/* find command */
	for (i=0; i<priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0) {
			ret = item->callback (priv, values, error);
			goto out;
		}
	}

	/* not found */
	string = g_string_new ("");
	/* TRANSLATORS: error message */
	g_string_append_printf (string, "%s\n", _("Command not found, valid commands are:"));
	for (i=0; i<priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		g_string_append_printf (string, " * %s\n", item->name);
	}
	g_set_error_literal (error, 1, 0, string->str);
	g_string_free (string, TRUE);
out:
	return ret;
}

/**
 * zif_cmd_shell:
 **/
static gboolean
zif_cmd_shell (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gchar buffer[1024];
	gchar *old_buffer = NULL;
	gchar **split;
	GError *error_local = NULL;
	GPtrArray *array;
	GPtrArray *stores_remote = NULL;
	GTimer *timer;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* use a timer to tell how long each thing took */
	timer = g_timer_new ();

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add remote */
				   19, /* find remote */
				   80, /* run transaction */
				   -1);
	if (!ret)
		goto out;

	/* add remote */
	state_local = zif_state_get_child (priv->state);
	stores_remote = zif_store_array_new ();
	ret = zif_store_array_add_remote_enabled (stores_remote, state_local, error);
	if (!ret)
		goto out;

	/* setup transaction */
	transaction = zif_transaction_new ();
	zif_transaction_set_euid (transaction, priv->uid);
	zif_transaction_set_cmdline (transaction, priv->cmdline);
	zif_transaction_set_store_local (transaction, priv->store_local);
	zif_transaction_set_stores_remote (transaction, stores_remote);

	/* are we running super verbose? */
	zif_transaction_set_verbose (transaction,
				     g_getenv ("ZIF_DEPSOLVE_DEBUG") != NULL);

	g_print ("\n");
	g_print (_("Welcome to the shell. Type '%s' to finish."), "exit");
	do {
		g_print ("\n(took %.1fms) Zif> ", g_timer_elapsed (timer, NULL) * 1000);
		fgets (buffer, 1024, stdin);
		g_strdelimit (buffer, "\n", '\0');

		/* reset timer */
		g_timer_reset (timer);

		/* no input */
		if (g_strcmp0 (buffer, "") == 0)
			continue;

		/* save this so "." works */
		if (g_strcmp0 (buffer, ".") == 0) {
			g_strlcpy (buffer, old_buffer, 1024);
		} else {
			g_free (old_buffer);
			old_buffer = g_strdup (buffer);
		}

		/* parse commands */
		split = g_strsplit (buffer, " ", -1);
		if (g_strcmp0 (split[0], "exit") == 0) {
			break;
		}

		/* reset the transaction */
		if (g_strcmp0 (split[0], "reset") == 0) {
			zif_transaction_reset (transaction);
			continue;
		}

		/* show the transaction */
		if (g_strcmp0 (split[0], "show") == 0) {
			zif_main_show_transaction (transaction);
			continue;
		}

		/* resolve the transaction */
		if (g_strcmp0 (split[0], "resolve") == 0 &&
		    split[1] == NULL) {
			zif_state_reset (priv->state);
			ret = zif_transaction_resolve (transaction, priv->state, &error_local);
			if (!ret) {
				g_print ("%s\n", error_local->message);
				g_clear_error (&error_local);
			}
			continue;
		}

		/* prepare the transaction */
		if (g_strcmp0 (split[0], "prepare") == 0) {
			zif_state_reset (priv->state);
			ret = zif_transaction_prepare (transaction, priv->state, &error_local);
			if (!ret) {
				g_print ("%s\n", error_local->message);
				g_clear_error (&error_local);
			}
			continue;
		}

		/* commit the transaction */
		if (g_strcmp0 (split[0], "commit") == 0) {
			zif_state_reset (priv->state);
			ret = zif_transaction_commit_full (transaction,
							   0,
							   priv->state,
							   &error_local);
			if (!ret) {
				g_print ("%s\n", error_local->message);
				g_clear_error (&error_local);
			}
			zif_transaction_reset (transaction);
			continue;
		}

		/* install a package */
		if (g_strcmp0 (split[0], "install") == 0) {
			zif_state_reset (priv->state);
			if (zif_package_id_check (split[1])) {
				package = zif_store_array_find_package (stores_remote, split[1], priv->state, &error_local);
				if (package == NULL) {
					g_print ("%s\n", error_local->message);
					g_clear_error (&error_local);
				} else {
					ret = zif_transaction_add_install (transaction, package, &error_local);
					if (!ret) {
						g_print ("%s\n", error_local->message);
						g_clear_error (&error_local);
					}
					g_object_unref (package);
				}
			} else {
				array = zif_store_array_resolve_full (stores_remote,
								      &split[1],
								      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
								      ZIF_STORE_RESOLVE_FLAG_USE_GLOB,
								      priv->state,
								      &error_local);
				if (array == NULL) {
					g_print ("%s\n", error_local->message);
					g_clear_error (&error_local);
					continue;
				}
				for (i=0; i<array->len; i++) {
					package = g_ptr_array_index (array, i);
					ret = zif_transaction_add_install (transaction, package, &error_local);
					if (!ret) {
						g_print ("%s\n", error_local->message);
						g_clear_error (&error_local);
					}
				}
				g_ptr_array_unref (array);
			}
			continue;
		}

		/* remove a package */
		if (g_strcmp0 (split[0], "remove") == 0) {
			zif_state_reset (priv->state);
			if (zif_package_id_check (split[1])) {
				package = zif_store_find_package (priv->store_local, split[1], priv->state, &error_local);
				if (package == NULL) {
					g_print ("%s\n", error_local->message);
					g_clear_error (&error_local);
				} else {
					ret = zif_transaction_add_remove (transaction, package, &error_local);
					if (!ret) {
						g_print ("%s\n", error_local->message);
						g_clear_error (&error_local);
					}
					g_object_unref (package);
				}
			} else {
				array = zif_store_resolve (priv->store_local, &split[1], priv->state, &error_local);
				if (array == NULL) {
					g_print ("%s\n", error_local->message);
					g_clear_error (&error_local);
					continue;
				}
				for (i=0; i<array->len; i++) {
					package = g_ptr_array_index (array, i);
					ret = zif_transaction_add_remove (transaction, package, &error_local);
					if (!ret) {
						g_print ("%s\n", error_local->message);
						g_clear_error (&error_local);
					}
				}
				g_ptr_array_unref (array);
			}
			continue;
		}

		/* install an update */
		if (g_strcmp0 (split[0], "update") == 0) {
			zif_state_reset (priv->state);
			if (zif_package_id_check (split[1])) {
				package = zif_store_find_package (priv->store_local, split[1], priv->state, &error_local);
				if (package == NULL) {
					g_print ("%s\n", error_local->message);
					g_clear_error (&error_local);
				} else {
					ret = zif_transaction_add_update (transaction, package, &error_local);
					if (!ret) {
						g_print ("%s\n", error_local->message);
						g_clear_error (&error_local);
					}
					g_object_unref (package);
				}
			} else {
				array = zif_store_resolve (priv->store_local, &split[1], priv->state, &error_local);
				if (array == NULL) {
					g_print ("%s\n", error_local->message);
					g_clear_error (&error_local);
					continue;
				}
				for (i=0; i<array->len; i++) {
					package = g_ptr_array_index (array, i);
					ret = zif_transaction_add_update (transaction, package, &error_local);
					if (!ret) {
						g_print ("%s\n", error_local->message);
						g_clear_error (&error_local);
					}
				}
				g_ptr_array_unref (array);
			}
			continue;
		}

		/* try to run normal commands */
		zif_state_reset (priv->state);
		g_print ("Warning: running non-native command, do not use for profiling...\n");
		ret = zif_cmd_run (priv, split[0], &split[1], &error_local);
		if (!ret) {
			g_print ("%s\n", error_local->message);
			g_clear_error (&error_local);
		}
	} while (TRUE);
out:
	if (transaction != NULL)
		g_object_unref (transaction);
	if (stores_remote != NULL)
		g_ptr_array_unref (stores_remote);
	g_timer_destroy (timer);
	g_free (old_buffer);
	return ret;
}

/**
 * zif_cmd_check:
 **/
static gboolean
zif_cmd_check (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* TRANSLATORS: used when the install database is being checked */
	zif_progress_bar_start (priv->progressbar, _("Checking database"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   10, /* get installed array */
				   5, /* add packages */
				   85, /* resolve */
				   -1);
	if (!ret)
		goto out;

	/* get installed array */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_get_packages (priv->store_local, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* update these packages */
	transaction = zif_transaction_new ();
	zif_transaction_set_euid (transaction, priv->uid);
	zif_transaction_set_cmdline (transaction, priv->cmdline);
	zif_transaction_set_store_local (transaction, priv->store_local);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_transaction_add_install (transaction, package, error);
		if (!ret)
			goto out;
	}

	/* are we running super verbose? */
	zif_transaction_set_verbose (transaction,
				     g_getenv ("ZIF_DEPSOLVE_DEBUG") != NULL);

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* run what we've got */
	state_local = zif_state_get_child (priv->state);
	ret = zif_transaction_resolve (transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);
out:
	if (transaction != NULL)
		g_object_unref (transaction);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_db_set:
 **/
static gboolean
zif_cmd_db_set (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	guint i;
	ZifDb *db = NULL;
	ZifPackage *package;

	/* enough arguments */
	if (g_strv_length (values) != 3) {
		g_set_error_literal (error,
				     1, 0,
				     /* TRANSLATORS: error code */
				     "Invalid argument, need '<package> <key> <value>'");
		goto out;
	}

	/* TRANSLATORS: used when the install database is being set */
	zif_progress_bar_start (priv->progressbar, _("Setting key"));

	/* get package */
	db = zif_db_new ();
	array = zif_db_get_packages (db, error);
	if (array == NULL)
		goto out;

	/* find something */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		if (g_strcmp0 (values[0],
			       zif_package_get_name (package)) == 0) {
			ret = TRUE;
			break;
		}
	}

	/* failed */
	if (!ret) {
		g_set_error (error,
			     1, 0,
			     /* TRANSLATORS: error code */
			     "Cannot find installed package '%s' in database",
			     values[0]);
		goto out;
	}

	/* set the value */
	ret = zif_db_set_string (db,
				 package,
				 values[1],
				 values[2],
				 error);
	if (!ret)
		goto out;

	/* print */
	zif_progress_bar_end (priv->progressbar);
	g_print ("%s  = %s\n", values[1], values[2]);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (db != NULL)
		g_object_unref (db);
	return ret;
}

/**
 * zif_cmd_db_get:
 **/
static gboolean
zif_cmd_db_get (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	gchar *value = NULL;
	GPtrArray *array = NULL;
	guint i;
	ZifDb *db = NULL;
	ZifPackage *package;

	/* enough arguments */
	if (g_strv_length (values) != 2) {
		g_set_error_literal (error,
				     1, 0,
				     /* TRANSLATORS: error code */
				     "Invalid argument, need '<package> <key>'");
		goto out;
	}

	/* TRANSLATORS: used when the install database is being set */
	zif_progress_bar_start (priv->progressbar, _("Getting key"));

	/* get package */
	db = zif_db_new ();
	array = zif_db_get_packages (db, error);
	if (array == NULL)
		goto out;

	/* find something */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		if (g_strcmp0 (values[0],
			       zif_package_get_name (package)) == 0) {
			ret = TRUE;
			break;
		}
	}

	/* failed */
	if (!ret) {
		g_set_error (error,
			     1, 0,
			     /* TRANSLATORS: error code */
			     "Cannot find installed package '%s' in database",
			     values[0]);
		goto out;
	}

	/* set the value */
	value = zif_db_get_string (db,
				   package,
				   values[1],
				   error);
	if (value == NULL) {
		ret = FALSE;
		goto out;
	}

	/* print */
	zif_progress_bar_end (priv->progressbar);
	g_print ("%s = %s\n", values[1], value);
out:
	g_free (value);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (db != NULL)
		g_object_unref (db);
	return ret;
}

/**
 * zif_cmd_db_remove:
 **/
static gboolean
zif_cmd_db_remove (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	guint i;
	ZifDb *db = NULL;
	ZifPackage *package;

	/* enough arguments */
	if (g_strv_length (values) != 2) {
		g_set_error_literal (error,
				     1, 0,
				     /* TRANSLATORS: error code */
				     "Invalid argument, need '<package> <key>'");
		goto out;
	}

	/* TRANSLATORS: used when the install database is being set */
	zif_progress_bar_start (priv->progressbar, _("Deleting key"));

	/* get package */
	db = zif_db_new ();
	array = zif_db_get_packages (db, error);
	if (array == NULL)
		goto out;

	/* find something */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		if (g_strcmp0 (values[0],
			       zif_package_get_name (package)) == 0) {
			ret = TRUE;
			break;
		}
	}

	/* failed */
	if (!ret) {
		g_set_error (error,
			     1, 0,
			     /* TRANSLATORS: error code */
			     "Cannot find installed package '%s' in database",
			     values[0]);
		goto out;
	}

	/* set the value */
	ret = zif_db_remove (db,
			     package,
			     values[1],
			     error);
	if (!ret)
		goto out;

	/* print */
	zif_progress_bar_end (priv->progressbar);

	/* TRANSLATORS: this is when the database key is deleted, e.g.
	 * "from_repo deleted"); */
	g_print ("%s %s\n", values[1], _("deleted"));
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (db != NULL)
		g_object_unref (db);
	return ret;
}

/**
 * zif_cmd_db_list:
 **/
static gboolean
zif_cmd_db_list (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	const gchar *key;
	gboolean ret = FALSE;
	gchar *value;
	GPtrArray *array = NULL;
	GPtrArray *keys = NULL;
	guint i;
	guint j;
	guint max = 0;
	ZifDb *db = NULL;
	ZifPackage *package;

	/* enough arguments */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     1, 0,
				     /* TRANSLATORS: error code */
				     "Invalid argument, need '<package>'");
		goto out;
	}

	/* TRANSLATORS: used when the install database is listed */
	zif_progress_bar_start (priv->progressbar, _("Listing keys"));

	/* get package */
	db = zif_db_new ();
	array = zif_db_get_packages (db, error);
	if (array == NULL)
		goto out;

	/* find something */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		if (g_strcmp0 (values[0],
			       zif_package_get_name (package)) == 0) {
			ret = TRUE;
			break;
		}
	}

	/* failed */
	if (!ret) {
		g_set_error (error,
			     1, 0,
			     /* TRANSLATORS: error code */
			     "Cannot find installed package '%s' in database",
			     values[0]);
		goto out;
	}

	/* set the value */
	keys = zif_db_get_keys (db,
				package,
				error);
	if (keys == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get the padding required */
	for (i=0;i<keys->len;i++) {
		key = g_ptr_array_index (keys, i);
		j = strlen (key);
		if (j > max)
			max = j;
	}

	/* print */
	zif_progress_bar_end (priv->progressbar);
	for (i=0; i<keys->len; i++) {
		key = g_ptr_array_index (keys, i);

		/* set the value */
		value = zif_db_get_string (db,
					   package,
					   key,
					   error);
		if (value == NULL) {
			ret = FALSE;
			goto out;
		}

		/* print */
		g_print ("%s", key);
		for (j=0; j<max - strlen (key); j++)
			g_print (" ");
		g_print (" = %s\n", value);
		g_free (value);
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (keys != NULL)
		g_ptr_array_unref (keys);
	if (db != NULL)
		g_object_unref (db);
	return ret;
}

/**
 * zif_cmd_print_deptree:
 *
 * Prints a tree of all packages that depends (directly or indirectly)
 * on the provided ZifPackage. This function is recursive.
 **/
static gboolean
zif_create_deptree (ZifPackage *package,
		    ZifState *state,
		    ZifStore *store,
		    ZifStore *store_processed,
		    guint depth,
		    GString *str,
		    GError **error)
{
	const gchar *pkg_id; /* the package_id of pkg */
	gboolean ret;
	GPtrArray *packages = NULL;
	GPtrArray *provides = NULL;
	guint i;
	guint our_depth;
	guint x;
	ZifPackage *current_package;
	ZifPackage *pkg; /* the package we pull from the store */
	ZifState *state_local;

	zif_state_set_number_steps (state, 2);

	/* Find out what "package" provides */
	state_local = zif_state_get_child (state);
	provides = zif_package_get_provides (package,
					     state_local,
					     error);
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* Get all packages which require what "package" provides
	 * TODO: Filter out (or mark in a special way) packages which
	 * requires provides that other packages can supply. */
	state_local = zif_state_get_child (state);
	packages = zif_store_what_requires (store,
					    provides,
					    state_local,
					    error);
	if (packages == NULL) {
		ret = FALSE;
		goto out;
	}
	zif_package_array_filter_duplicates (packages);

	/* this step done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* add package to the store as we've already showed it
	 * before this function was called */
	zif_store_add_package (store_processed, package, NULL);

	/* process all packages we got before */
	for (i = 0; i<packages->len; i++) {

		zif_state_reset(state);
		state_local = zif_state_get_child (state);

		/* Figure out if we processed this package already */
		current_package = g_ptr_array_index (packages, i);
		pkg_id = zif_package_get_id (current_package);
		pkg = zif_store_find_package (store_processed,
					      pkg_id,
					      state_local,
					      NULL);
		if (pkg == NULL) {

			zif_state_reset (state);
			g_string_append (str, "\n");

			/* draw tree */
			for (x=0; x<depth; x++) {
				g_string_append (str, "| ");
				if (depth > 0)
					g_string_append (str, " ");
			}
			if (i == packages->len-1)
				g_string_append (str, "`");
			else
				g_string_append (str, "|");

			/* print the package name and arch */
			g_string_append_printf (str, "--%s",
						zif_package_get_name_arch (current_package));

			/* so we'd know we already processed it */
			ret = zif_store_add_package (store_processed,
						     current_package,
						     error);
			if (!ret)
				goto out;

			/* limit recursion */
			if (depth < 1000) {
				our_depth = depth + 1;
				state_local = zif_state_get_child (state);
				ret = zif_create_deptree (current_package,
							  state_local,
							  store,
							  store_processed,
							  our_depth,
							  str,
							  error);
				if (!ret)
					goto out;
			}
		} else {
			if (pkg != NULL)
				g_object_unref (pkg);
		}
	}
out:
	if (provides != NULL)
		g_ptr_array_unref (provides);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	return ret;
}


static gboolean
zif_cmd_deptree (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *resolved_packages;
	GString *tree = NULL;
	guint i;
	ZifPackage *package_tmp;
	ZifState *state_local;
	ZifState *state_loop;
	ZifStore *store_processed;

	/* enough arguments */
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     1, 0,
				     /* TRANSLATORS: error code */
				     "Invalid argument, need '<package>'");
		goto out;
	}

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   45, /* resolve */
				   55, /* get deps */
				   -1);
	if (!ret)
		goto out;

	tree = g_string_new ("");
	store_processed = zif_store_meta_new ();

	/* get packages */
	state_local = zif_state_get_child (priv->state);
	resolved_packages = zif_store_resolve (priv->store_local,
					       values,
					       state_local,
					       error);
	if (resolved_packages == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this step done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* failed */
	if (resolved_packages->len == 0) {
		ret = FALSE;
		g_set_error (error,
			     1, 0,
			     /* TRANSLATORS: error code */
			     "Cannot find installed package '%s'",
			     values[0]);
		goto out;
	}

	/* get the deptree for each package */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local,
				    resolved_packages->len);
	for (i = 0; i < resolved_packages->len; i++) {
		package_tmp = g_ptr_array_index (resolved_packages, i);

		/* split up packages */
		if (i > 0)
			g_string_append (tree, "\n\n");

		g_string_append_printf (tree, "%s",
					zif_package_get_printable (package_tmp));
		state_loop = zif_state_get_child (state_local);
		zif_state_set_report_progress (state_loop, FALSE);
		ret = zif_create_deptree (package_tmp,
					  state_loop,
					  priv->store_local,
					  store_processed,
					  0,
					  tree,
					  error);
		if (!ret)
			goto out;

		/* this step done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this step done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* print */
	zif_progress_bar_end (priv->progressbar);
	g_print ("%s\n", tree->str);
out:
	if (tree != NULL)
		g_string_free (tree, TRUE);
	return ret;
}

/**
 * zif_take_lock_cb:
 **/
static gboolean
zif_take_lock_cb (ZifState *state,
		  ZifLock *lock,
		  ZifLockType lock_type,
		  GError **error,
		  gpointer user_data)
{
	guint i;
	gboolean ret = FALSE;
	guint lock_delay;
	guint lock_retries;
	GError *error_local = NULL;
	ZifCmdPrivate *priv = (ZifCmdPrivate *) user_data;

	/* ZifLock */
	lock_retries = zif_config_get_uint (priv->config,
					    "lock_retries",
					    NULL);
	lock_delay = zif_config_get_uint (priv->config,
					  "lock_delay",
					  NULL);
	for (i=0; i<lock_retries; i++) {

		/* try to take */
		ret = zif_lock_take (lock, lock_type, &error_local);
		if (ret)
			break;

		/* this one is really fatal */
		if (error_local->domain == ZIF_LOCK_ERROR &&
		    error_local->code == ZIF_LOCK_ERROR_PERMISSION) {
			g_propagate_error (error, error_local);
			goto out;
		}
		g_print ("Failed to lock on try %i of %i (sleeping for %ims)\n",
			 i+1, lock_retries, lock_delay);
		g_debug ("failed to lock: %s", error_local->message);
		if (i == lock_retries - 1) {
			g_propagate_error (error, error_local);
			goto out;
		}
		g_clear_error (&error_local);
		g_usleep (lock_delay * 1000);
	}
out:
	return ret;
}

/**
 * pk_error_handler_cb:
 */
static gboolean
pk_error_handler_cb (const GError *error, gpointer user_data)
{
	ZifCmdPrivate *priv = (ZifCmdPrivate *) user_data;
	gboolean skip_broken;

	/* what does the config file say? */
	skip_broken = zif_config_get_boolean (priv->config,
					      "skip_broken",
					      NULL);
	if (!skip_broken)
		return FALSE;

	/* emit a warning, this isn't fatal */
	g_debug ("non-fatal error: %s",
		 error->message);
	return TRUE;
}

static gboolean
zif_main_set_stores_runtime_enable (ZifCmdPrivate *priv,
				    const gchar *repo_str,
				    gboolean enabled,
				    GError **error)
{
	gboolean ret = TRUE;
	gchar **repos = NULL;
	guint i;
	ZifStoreRemote *store;

	/* no input */
	if (repo_str == NULL)
		goto out;

	/* enable or disable each one */
	repos = g_strsplit (repo_str, ",", -1);
	for (i=0; repos[i] != NULL; i++) {
		zif_state_reset (priv->state);
		store = zif_repos_get_store (priv->repos,
					     repos[i],
					     priv->state,
					     error);
		if (store == NULL) {
			ret = FALSE;
			goto out;
		}
		zif_store_set_enabled (ZIF_STORE (store), enabled);
	}
out:
	g_strfreev (repos);
	return ret;
}

#if GLIB_CHECK_VERSION(2,29,19)

/**
 * zif_main_sigint_cb:
 **/
static gboolean
zif_main_sigint_cb (gpointer user_data)
{
	GCancellable *cancellable;
	ZifCmdPrivate *priv = (ZifCmdPrivate *) user_data;

	g_debug ("Handling SIGINT");

	/* TRANSLATORS: the user just did ctrl-c */
	zif_progress_bar_set_action (priv->progressbar, _("Cancellation in progress..."));

	/* cancel any tasks still running */
	if (priv->state != NULL) {
		cancellable = zif_state_get_cancellable (priv->state);
		g_cancellable_cancel (cancellable);
	}

	return FALSE;
}

#else

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
		/* TRANSLATORS: the user just did ctrl-c */
		g_print ("%s\n", _("Cancellation in progress..."));
		g_cancellable_cancel (cancellable);
	}
}

#endif

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean assume_no = FALSE;
	gboolean assume_yes = FALSE;
	gboolean background = FALSE;
	gboolean offline = FALSE;
	gboolean profile = FALSE;
	gboolean ret;
	gboolean skip_broken = FALSE;
	gboolean exact_arch = FALSE;
	gboolean verbose = FALSE;
	gboolean distro_sync = FALSE;
	gchar *cmd_descriptions = NULL;
	gchar *config_file = NULL;
	gchar *excludes = NULL;
	gchar *http_proxy = NULL;
	gchar *options_help = NULL;
	gchar *root = NULL;
	gchar *enablerepo = NULL;
	gchar *disablerepo = NULL;
	GError *error = NULL;
	gint retval = 0;
	gint terminal_cols = 0;
	guint age = 0;
	struct winsize w;
	ZifCmdPrivate *priv;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "profile", '\0', 0, G_OPTION_ARG_NONE, &profile,
			_("Enable low level profiling of Zif"), NULL },
		{ "background", 'b', 0, G_OPTION_ARG_NONE, &background,
			_("Enable background mode to run using less CPU"), NULL },
		{ "offline", 'o', 0, G_OPTION_ARG_NONE, &offline,
			_("Work offline when possible"), NULL },
		{ "distro-sync", '\0', 0, G_OPTION_ARG_NONE, &distro_sync,
			_("Take into account distribution versions when calculating updates"), NULL },
		{ "config", 'c', 0, G_OPTION_ARG_STRING, &config_file,
			_("Use different config file"), NULL },
		{ "excludes", 'c', 0, G_OPTION_ARG_STRING, &excludes,
			_("Exclude certain packages"), NULL },
		{ "root", 'c', 0, G_OPTION_ARG_STRING, &root,
			_("Use different rpm database root"), NULL },
		{ "proxy", 'p', 0, G_OPTION_ARG_STRING, &http_proxy,
			_("Proxy server setting"), NULL },
		{ "age", 'a', 0, G_OPTION_ARG_INT, &age,
			_("Permitted age of the cache in seconds, 0 for never (default)"), NULL },
		{ "skip-broken", 's', 0, G_OPTION_ARG_NONE, &skip_broken,
			_("Skip broken dependencies and repos rather than failing"), NULL },
		{ "exact-arch", 'x', 0, G_OPTION_ARG_NONE, &exact_arch,
			_("Only use the exact architecture packages for this machine"), NULL },
		{ "assume-yes", 'y', 0, G_OPTION_ARG_NONE, &assume_yes,
			_("Assume yes to all questions"), NULL },
		{ "assume-no", 'n', 0, G_OPTION_ARG_NONE, &assume_no,
			_("Assume no to all questions"), NULL },
		{ "enablerepo", '\0', 0, G_OPTION_ARG_STRING, &enablerepo,
			_("Enable one or more repositories"), NULL },
		{ "disablerepo", '\0', 0, G_OPTION_ARG_STRING, &disablerepo,
			_("Disable one or more repositories"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* create helper object */
	priv = g_new0 (ZifCmdPrivate, 1);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();
	zif_init ();

	priv->context = g_option_context_new ("ZIF Console Program");
	g_option_context_add_main_entries (priv->context, options, NULL);
	g_option_context_parse (priv->context, &argc, &argv, NULL);

	priv->progressbar = zif_progress_bar_new ();
	zif_progress_bar_set_on_console (priv->progressbar,
					 !verbose &&
					 isatty (fileno (stdout)) == 1);

	/* 'Checking manifests' is the longest title, so 18 chars */
	zif_progress_bar_set_padding (priv->progressbar, 18);

	ioctl (0, TIOCGWINSZ, &w);
	terminal_cols = w.ws_col;
	terminal_cols -= 19; /* title padding plus space */
	terminal_cols -= 11; /* percentage */
	if (terminal_cols < 0)
		terminal_cols = 0;
	zif_progress_bar_set_size (priv->progressbar, terminal_cols);

	/* save in the private data */
	priv->assume_no = assume_no;

#if GLIB_CHECK_VERSION(2,29,19)
	/* do stuff on ctrl-c */
	g_unix_signal_add (SIGINT,
			   zif_main_sigint_cb,
			   priv);
#else
	signal (SIGINT, zif_main_sigint_cb);
#endif

	/* don't let GIO start it's own session bus */
	g_unsetenv ("DBUS_SESSION_BUS_ADDRESS");

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
		g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
		g_log_set_handler ("Zif", G_LOG_LEVEL_DEBUG,
				   zif_log_ignore_cb, NULL);
	}

	/* put this in the logs to help debugging */
	g_debug ("Zif version %i.%i.%i",
		 ZIF_MAJOR_VERSION,
		 ZIF_MINOR_VERSION,
		 ZIF_MICRO_VERSION);

	/* fallback */
	if (config_file == NULL)
		config_file = g_build_filename (SYSCONFDIR, "zif", "zif.conf", NULL);
	if (root == NULL)
		root = g_strdup ("/");

	/* ZifConfig */
	priv->config = zif_config_new ();
	ret = zif_config_set_filename (priv->config, config_file, &error);
	if (!ret) {
		g_error ("failed to set config: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_config_set_string (priv->config, "http_proxy", http_proxy, &error);
	if (!ret) {
		g_error ("failed to set proxy: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_config_set_string (priv->config, "prefix", root, &error);
	if (!ret) {
		g_error ("failed to set prefix: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_config_set_string (priv->config, "excludes", excludes, &error);
	if (!ret) {
		g_error ("failed to set excludes: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_config_set_boolean (priv->config, "skip_broken", skip_broken, &error);
	if (!ret) {
		g_error ("failed to set skip_broken: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_config_set_boolean (priv->config, "exactarch", exact_arch, &error);
	if (!ret) {
		g_error ("failed to set exactarch: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_config_set_boolean (priv->config, "assumeyes", assume_yes, &error);
	if (!ret) {
		g_error ("failed to set assumeyes: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = zif_config_set_boolean (priv->config, "background", background, &error);
	if (!ret) {
		g_error ("failed to set background: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* disto sync? */
	if (distro_sync) {
		ret = zif_config_set_string (priv->config,
					     "pkg_compare_mode",
					     "distro",
					     &error);
		if (!ret) {
			g_error ("failed to set pkg_compare_mode: %s",
				 error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* save the command line in case we modify the history db */
	priv->cmdline = g_strjoinv (" ", argv);

	/* are we root? */
	priv->uid = getuid ();
	if (priv->uid != 0) {
		if (age != 0) {
			/* TRANSLATORS: we can't run as the user */
			g_print ("%s\n", _("Cannot specify age when not a privileged user."));
			goto out;
		}
		if (!offline) {
			/* TRANSLATORS: we can't download new stuff as a user */
			g_print ("%s\n", _("Enabling offline mode as user unprivileged."));
			offline = TRUE;
		}
	}

	/* are we allowed to access the repos */
	if (!offline)
		zif_config_set_boolean (priv->config, "network", TRUE, NULL);

	/* set the maximum age of the repo data */
	if (age > 0)
		zif_config_set_uint (priv->config, "metadata_expire", age, NULL);

	/* ZifState */
	priv->state = zif_state_new ();
	zif_state_set_enable_profile (priv->state, profile);
	g_signal_connect (priv->state, "percentage-changed",
			  G_CALLBACK (zif_state_percentage_changed_cb),
			  priv->progressbar);
	g_signal_connect (priv->state, "allow-cancel-changed",
			  G_CALLBACK (zif_state_allow_cancel_changed_cb),
			  priv->progressbar);
	g_signal_connect (priv->state, "action-changed",
			  G_CALLBACK (zif_state_action_changed_cb),
			  priv->progressbar);
	g_signal_connect (priv->state, "notify::speed",
			  G_CALLBACK (zif_state_speed_changed_cb),
			  priv->progressbar);
	zif_state_set_lock_handler (priv->state,
				    zif_take_lock_cb,
				    priv);

	/* always set this, even if --skip-broken isn't set so we can
	 * override the config file at runtime */
	zif_state_set_error_handler (priv->state,
				     pk_error_handler_cb,
				     priv);

	/* reference this as a singleton */
	priv->store_local = zif_store_local_new ();

	/* process the repo add and disables */
	if (enablerepo != NULL ||
	    disablerepo != NULL) {
		priv->repos = zif_repos_new ();
		ret = zif_main_set_stores_runtime_enable (priv,
							  enablerepo,
							  TRUE,
							  &error);
		if (!ret)
			goto error;
		ret = zif_main_set_stores_runtime_enable (priv,
							  disablerepo,
							  FALSE,
							  &error);
		if (!ret)
			goto error;

		/* restore progress */
		zif_state_reset (priv->state);
	}

#if !GLIB_CHECK_VERSION(2,29,19)
	/* for the signal handler */
	_state = priv->state;
#endif

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_cmd_item_free);
	zif_cmd_add (priv->cmd_array,
		     "clean",
		     /* TRANSLATORS: command description */
		     _("Remove cached data"),
		     zif_cmd_clean);
	zif_cmd_add (priv->cmd_array,
		     "download",
		     /* TRANSLATORS: command description */
		     _("Download a package"),
		     zif_cmd_download);
	zif_cmd_add (priv->cmd_array,
		     "find-package",
		     /* TRANSLATORS: command description */
		     _("Find a given package given the ID"),
		     zif_cmd_find_package);
	zif_cmd_add (priv->cmd_array,
		     "get-categories",
		     /* TRANSLATORS: command description */
		     _("Returns the list of categories"),
		     zif_cmd_get_categories);
	zif_cmd_add (priv->cmd_array,
		     "get-depends,deplist",
		     /* TRANSLATORS: command description */
		     _("List a package's dependencies"),
		     zif_cmd_get_depends);
	zif_cmd_add (priv->cmd_array,
		     "get-details,info",
		     /* TRANSLATORS: command description */
		     _("Display details about a package or group of packages"),
		     zif_cmd_get_details);
	zif_cmd_add (priv->cmd_array,
		     "get-files",
		     /* TRANSLATORS: command description */
		     _("List the files in a package"),
		     zif_cmd_get_files);
	zif_cmd_add (priv->cmd_array,
		     "get-groups",
		     /* TRANSLATORS: command description */
		     _("Get the groups the system supports"),
		     zif_cmd_get_groups);
	zif_cmd_add (priv->cmd_array,
		     "get-packages,list",
		     /* TRANSLATORS: command description */
		     _("List all packages"),
		     zif_cmd_get_packages);
	zif_cmd_add (priv->cmd_array,
		     "get-updates,check-update",
		     /* TRANSLATORS: command description */
		     _("Check for available package updates"),
		     zif_cmd_get_updates);
	zif_cmd_add (priv->cmd_array,
		     "get-upgrades",
		     /* TRANSLATORS: command description */
		     _("Check for newer operating system versions"),
		     zif_cmd_get_upgrades);
	zif_cmd_add (priv->cmd_array,
		     "get-config-value",
		     /* TRANSLATORS: command description */
		     _("Get an expanded value from the config file"),
		     zif_cmd_get_config_value);
	zif_cmd_add (priv->cmd_array,
		     "build-depends,builddep",
		     /* TRANSLATORS: command description */
		     _("Installs the build dependencies for a given package"),
		     zif_cmd_build_depends);
	zif_cmd_add (priv->cmd_array,
		     "help",
		     /* TRANSLATORS: command description */
		     _("Display a helpful usage message"),
		     zif_cmd_help);
	zif_cmd_add (priv->cmd_array,
		     "install",
		     /* TRANSLATORS: command description */
		     _("Install a package"),
		     zif_cmd_install);
	zif_cmd_add (priv->cmd_array,
		     "downgrade",
		     /* TRANSLATORS: command description */
		     _("Downgrade a package to a previous version"),
		     zif_cmd_downgrade);
	zif_cmd_add (priv->cmd_array,
		     "local-install,localinstall",
		     /* TRANSLATORS: command description */
		     _("Install a local package"),
		     zif_cmd_local_install);
	zif_cmd_add (priv->cmd_array,
		     "manifest-check",
		     /* TRANSLATORS: command description */
		     _("Check a transaction manifest"),
		     zif_cmd_manifest_check);
	zif_cmd_add (priv->cmd_array,
		     "manifest-dump",
		     /* TRANSLATORS: command description */
		     _("Dump a transaction manifest to a file"),
		     zif_cmd_manifest_dump);
	zif_cmd_add (priv->cmd_array,
		     "refresh-cache,makecache",
		     /* TRANSLATORS: command description */
		     _("Generate the metadata cache"),
		     zif_cmd_refresh_cache);
	zif_cmd_add (priv->cmd_array,
		     "remove,erase",
		     /* TRANSLATORS: command description */
		     _("Remove a package"),
		     zif_cmd_remove);
	zif_cmd_add (priv->cmd_array,
		     "repo-disable",
		     /* TRANSLATORS: command description */
		     _("Disable a specific software repository"),
		     zif_cmd_repo_disable);
	zif_cmd_add (priv->cmd_array,
		     "repo-enable",
		     /* TRANSLATORS: command description */
		     _("Enable a specific software repository"),
		     zif_cmd_repo_enable);
	zif_cmd_add (priv->cmd_array,
		     "repo-list,repolist",
		     /* TRANSLATORS: command description */
		     _("Display the configured software repositories"),
		     zif_cmd_repo_list);
	zif_cmd_add (priv->cmd_array,
		     "resolve",
		     /* TRANSLATORS: command description */
		     _("Find a given package name"),
		     zif_cmd_resolve);
	zif_cmd_add (priv->cmd_array,
		     "search-category",
		     /* TRANSLATORS: command description */
		     _("Search package details for the given category"),
		     zif_cmd_search_category);
	zif_cmd_add (priv->cmd_array,
		     "search-details,search",
		     /* TRANSLATORS: command description */
		     _("Search package details for the given string"),
		     zif_cmd_search_details);
	zif_cmd_add (priv->cmd_array,
		     "search-file",
		     /* TRANSLATORS: command description */
		     _("Search packages for the given filename"),
		     zif_cmd_search_file);
	zif_cmd_add (priv->cmd_array,
		     "search-group",
		     /* TRANSLATORS: command description */
		     _("Search packages in the given group"),
		     zif_cmd_search_group);
	zif_cmd_add (priv->cmd_array,
		     "search-name",
		     /* TRANSLATORS: command description */
		     _("Search package name for the given string"),
		     zif_cmd_search_name);
	zif_cmd_add (priv->cmd_array,
		     "shell",
		     /* TRANSLATORS: command description */
		     _("Run an interactive shell"),
		     zif_cmd_shell);
	zif_cmd_add (priv->cmd_array,
		     "update,upgrade",
		     /* TRANSLATORS: command description */
		     _("Update a package to the newest available version"),
		     zif_cmd_update);
	zif_cmd_add (priv->cmd_array,
		     "distro-sync,distribution-synchronization",
		     /* TRANSLATORS: command description */
		     _("Update a package taking into account distribution version"),
		     zif_cmd_distro_sync);
	zif_cmd_add (priv->cmd_array,
		     "update-details",
		     /* TRANSLATORS: command description */
		     _("Display details about an update"),
		     zif_cmd_update_details);
	zif_cmd_add (priv->cmd_array,
		     "upgrade-distro",
		     /* TRANSLATORS: command description */
		     _("Upgrade the operating system to a newer version"),
		     zif_cmd_upgrade);
	zif_cmd_add (priv->cmd_array,
		     "upgrade-distro-live",
		     /* TRANSLATORS: command description */
		     _("Live-upgrade the operating system to a newer version"),
		     zif_cmd_upgrade_distro_live);
	zif_cmd_add (priv->cmd_array,
		     "what-conflicts",
		     /* TRANSLATORS: command description */
		     _("Find what package conflicts with the given value"),
		     zif_cmd_what_conflicts);
	zif_cmd_add (priv->cmd_array,
		     "what-obsoletes",
		     /* TRANSLATORS: command description */
		     _("Find what package obsoletes the given value"),
		     zif_cmd_what_obsoletes);
	zif_cmd_add (priv->cmd_array,
		     "what-provides,provides",
		     /* TRANSLATORS: command description */
		     _("Find what package provides the given value"),
		     zif_cmd_what_provides);
	zif_cmd_add (priv->cmd_array,
		     "what-requires,resolvedep",
		     /* TRANSLATORS: command description */
		     _("Find what package requires the given value"),
		     zif_cmd_what_requires);
	zif_cmd_add (priv->cmd_array,
		     "check",
		     /* TRANSLATORS: command description */
		     _("Check for problems in the installed database"),
		     zif_cmd_check);
	zif_cmd_add (priv->cmd_array,
		     "db-get",
		     /* TRANSLATORS: command description */
		     _("Get a value in the package database"),
		     zif_cmd_db_get);
	zif_cmd_add (priv->cmd_array,
		     "db-set",
		     /* TRANSLATORS: command description */
		     _("Set a value in the installed package database"),
		     zif_cmd_db_set);
	zif_cmd_add (priv->cmd_array,
		     "db-remove",
		     /* TRANSLATORS: command description */
		     _("Remove a value from the installed package database"),
		     zif_cmd_db_remove);
	zif_cmd_add (priv->cmd_array,
		     "db-list",
		     /* TRANSLATORS: command description */
		     _("List values from the installed package database"),
		     zif_cmd_db_list);
	zif_cmd_add (priv->cmd_array,
		     "dep-provides",
		     /* TRANSLATORS: command description */
		     _("Gets the provides for a given package"),
		     zif_cmd_dep_provides);
	zif_cmd_add (priv->cmd_array,
		     "dep-requires",
		     /* TRANSLATORS: command description */
		     _("Gets the requires for a given package"),
		     zif_cmd_dep_requires);
	zif_cmd_add (priv->cmd_array,
		     "dep-conflicts",
		     /* TRANSLATORS: command description */
		     _("Gets the conflicts for a given package"),
		     zif_cmd_dep_conflicts);
	zif_cmd_add (priv->cmd_array,
		     "dep-obsoletes",
		     /* TRANSLATORS: command description */
		     _("Gets the obsoletes for a given package"),
		     zif_cmd_dep_obsoletes);
	zif_cmd_add (priv->cmd_array,
		     "history-list,history",
		     /* TRANSLATORS: command description */
		     _("Gets the transaction history list"),
		     zif_cmd_history_list);
	zif_cmd_add (priv->cmd_array,
		     "history-package",
		     /* TRANSLATORS: command description */
		     _("Gets the transaction history for a specified package"),
		     zif_cmd_history_list);
	zif_cmd_add (priv->cmd_array,
		     "history-import",
		     /* TRANSLATORS: command description */
		     _("Imports the history data from a legacy database"),
		     zif_cmd_history_import);
	zif_cmd_add (priv->cmd_array,
		     "deptree",
		     /* TRANSLATORS: command description */
		     _("Shows a list of packages that depend on a specified package"),
		     zif_cmd_deptree);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) zif_sort_command_name_cb);

	/* get a list of the commands */
	cmd_descriptions = zif_cmd_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);

	/* nothing specified */
	if (argc < 2) {
		options_help = g_option_context_get_help (priv->context, TRUE, NULL);
		g_print ("%s", options_help);
		goto out;
	}

	/* run the specified command */
	ret = zif_cmd_run (priv, argv[1], (gchar**) &argv[2], &error);
error:
	zif_progress_bar_end (priv->progressbar);
	if (!ret) {
		const gchar *message;
		if (error->domain == ZIF_STATE_ERROR) {
			switch (error->code) {
			case ZIF_STATE_ERROR_CANCELLED:
				/* TRANSLATORS: error message */
				message = _("Cancelled");
				break;
			case ZIF_STATE_ERROR_INVALID:
				/* TRANSLATORS: error message */
				message = _("The system state was invalid");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled state error");
			}
		} else if (error->domain == ZIF_TRANSACTION_ERROR) {
			switch (error->code) {
			case ZIF_TRANSACTION_ERROR_FAILED:
				/* TRANSLATORS: error message */
				message = _("The transaction failed");
				break;
			case ZIF_TRANSACTION_ERROR_NOTHING_TO_DO:
				/* TRANSLATORS: error message */
				message = _("Nothing to do");
				break;
			case ZIF_TRANSACTION_ERROR_NOT_SUPPORTED:
				/* TRANSLATORS: error message */
				message = _("No supported");
				break;
			case ZIF_TRANSACTION_ERROR_CONFLICTING:
				/* TRANSLATORS: error message */
				message = _("The transaction conflicts");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled transaction error");
			}
		} else if (error->domain == ZIF_STORE_ERROR) {
			switch (error->code) {
			case ZIF_STORE_ERROR_FAILED:
				/* TRANSLATORS: error message */
				message = _("Failed to store");
				break;
			case ZIF_STORE_ERROR_FAILED_AS_OFFLINE:
				/* TRANSLATORS: error message */
				message = _("Failed as offline");
				break;
			case ZIF_STORE_ERROR_FAILED_TO_FIND:
				/* TRANSLATORS: error message */
				message = _("Failed to find");
				break;
			case ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD:
				/* TRANSLATORS: error message */
				message = _("Failed to download");
				break;
			case ZIF_STORE_ERROR_ARRAY_IS_EMPTY:
				/* TRANSLATORS: error message */
				message = _("Store array is empty");
				break;
			case ZIF_STORE_ERROR_NO_SUPPORT:
				/* TRANSLATORS: error message */
				message = _("Not supported");
				break;
			case ZIF_STORE_ERROR_NOT_LOCKED:
				/* TRANSLATORS: error message */
				message = _("Not locked");
				break;
			case ZIF_STORE_ERROR_MULTIPLE_MATCHES:
				/* TRANSLATORS: error message */
				message = _("There are multiple matches");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled store error");
			}
		} else if (error->domain == ZIF_PACKAGE_ERROR) {
			switch (error->code) {
			case ZIF_PACKAGE_ERROR_FAILED:
				/* TRANSLATORS: error message */
				message = _("Package operation failed");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled package error");
			}
		} else if (error->domain == ZIF_HISTORY_ERROR) {
			switch (error->code) {
			case ZIF_HISTORY_ERROR_FAILED:
				/* TRANSLATORS: error message */
				message = _("History operation failed");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled history error");
			}
		} else if (error->domain == ZIF_CONFIG_ERROR) {
			switch (error->code) {
			case ZIF_CONFIG_ERROR_FAILED:
				/* TRANSLATORS: error message */
				message = _("Settings operation failed");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled config error");
			}
		} else if (error->domain == ZIF_DOWNLOAD_ERROR) {
			switch (error->code) {
			case ZIF_DOWNLOAD_ERROR_FAILED:
				/* TRANSLATORS: error message */
				message = _("Download failed");
				break;
			case ZIF_DOWNLOAD_ERROR_PERMISSION_DENIED:
				/* TRANSLATORS: error message */
				message = _("Download failed as permission denied");
				break;
			case ZIF_DOWNLOAD_ERROR_NO_SPACE:
				/* TRANSLATORS: error message */
				message = _("No space left on device");
				break;
			case ZIF_DOWNLOAD_ERROR_CANCELLED:
				/* TRANSLATORS: error message */
				message = _("Download was cancelled");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled download error");
			}
		} else if (error->domain == ZIF_LOCK_ERROR) {
			switch (error->code) {
			case ZIF_LOCK_ERROR_FAILED:
				/* TRANSLATORS: error message */
				message = _("Failed");
				break;
			case ZIF_LOCK_ERROR_ALREADY_LOCKED:
				/* TRANSLATORS: error message */
				message = _("Already locked");
				break;
			case ZIF_LOCK_ERROR_NOT_LOCKED:
				/* TRANSLATORS: error message */
				message = _("Not locked");
				break;
			case ZIF_LOCK_ERROR_PERMISSION:
				/* TRANSLATORS: error message */
				message = _("No permissions");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled metadata error");
			}
		} else if (error->domain == ZIF_REPOS_ERROR) {
			switch (error->code) {
			case ZIF_REPOS_ERROR_FAILED:
				/* TRANSLATORS: error message */
				message = _("Failed");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled repos error");
			}
		} else if (error->domain == 1) {
			/* local error, already translated */
			message = NULL;
		} else {
			/* TRANSLATORS: we suck */
			message = _("Failed");
			g_warning ("%s:%i",
				   g_quark_to_string (error->domain),
				   error->code);
		}

		/* print translated errors */
		if (message != NULL)
			g_print ("%s: ", message);
		g_print ("%s\n", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}
out:
	if (priv != NULL) {
		g_object_unref (priv->progressbar);
		if (priv->store_local != NULL)
			g_object_unref (priv->store_local);
		if (priv->config != NULL)
			g_object_unref (priv->config);
		if (priv->state != NULL)
			g_object_unref (priv->state);
		if (priv->repos != NULL)
			g_object_unref (priv->repos);
		if (priv->cmd_array != NULL)
			g_ptr_array_unref (priv->cmd_array);
		g_option_context_free (priv->context);
		g_free (priv->cmdline);
		g_free (priv);
	}

	/* free state */
	g_free (enablerepo);
	g_free (disablerepo);
	g_free (root);
	g_free (cmd_descriptions);
	g_free (http_proxy);
	g_free (config_file);
	g_free (options_help);
	return retval;
}

