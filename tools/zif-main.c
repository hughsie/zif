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
 * zif_state_action_changed_cb:
 **/
static void
zif_state_action_changed_cb (ZifState *state, ZifStateAction action, const gchar *action_hint, ZifProgressBar *progressbar)
{
	gchar *hint = NULL;
	if (action == ZIF_STATE_ACTION_UNKNOWN)
		goto out;
	hint = g_strdup_printf ("%s: %s",
				zif_state_action_to_string (action),
				action_hint != NULL ? action_hint : "");
	zif_progress_bar_set_action (progressbar, hint);
out:
	g_free (hint);
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

typedef struct {
	gboolean		 skip_broken;
	gboolean		 assume_yes;
	GOptionContext		*context;
	GPtrArray		*cmd_array;
	ZifConfig		*config;
	ZifProgressBar		*progressbar;
	ZifRelease		*release;
	ZifState		*state;
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

/**
 * zif_cmd_add:
 **/
static void
zif_cmd_add (GPtrArray *array, const gchar *name, const gchar *description, ZifCmdPrivateCb callback)
{
	ZifCmdItem *item;
	item = g_new0 (ZifCmdItem, 1);
	item->name = g_strdup (name);
	item->description = g_strdup (description);
	item->callback = callback;
	g_ptr_array_add (array, item);
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
	guint max_len = 0;
	ZifCmdItem *item;
	GString *string;

	/* get maximum command length */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		len = strlen (item->name);
		if (len > max_len)
			max_len = len;
	}

	/* ensure we're spaced by at least this */
	if (max_len < 19)
		max_len = 19;

	/* print each command */
	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		len = strlen (item->name);
		for (j=len; j<max_len+3; j++)
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

	/* TRANSLATORS: performing action */
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
 * zif_cmd_download:
 **/
static gboolean
zif_cmd_download (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifState *state_local;
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
	array = zif_store_array_resolve (store_array, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "no package found");
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Downloading"));

	/* download package file */
	package = g_ptr_array_index (array, 0);
	state_local = zif_state_get_child (priv->state);
	ret = zif_package_remote_download (ZIF_PACKAGE_REMOTE (package),
					   "/tmp", state_local, error);
	if (!ret)
		goto out;

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
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	ZifPackage *package = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a package_id");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Resolving package"));

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
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_get_categories:
 **/
static gboolean
zif_cmd_get_categories (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifCategory *obj;
	ZifState *state_local;

	/* TRANSLATORS: performing action */
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
	ZifState *state_loop;
	GPtrArray *requires = NULL;
	ZifDepend *require;
	const gchar *require_str;
	GPtrArray *provides;
	const gchar *package_id;
	guint i, j;
	gchar **split;
	GString *string = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a package name");
		goto out;
	}

	/* TRANSLATORS: performing action */
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
	array = zif_store_array_resolve (store_array, (gchar**)values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "no package found");
		goto out;
	}
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
	for (i=0; i<requires->len; i++) {

		/* setup deeper state */
		state_loop = zif_state_get_child (state_local);

		require = g_ptr_array_index (requires, i);
		require_str = zif_depend_get_description (require);
		g_string_append_printf (string, "  dependency: %s\n", require_str);

		/* find the package providing the depend */
		provides = zif_store_array_what_provides (store_array, require, state_loop, error);
		if (provides == NULL) {
			ret = FALSE;
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
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
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
	if (requires != NULL)
		g_ptr_array_unref (requires);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
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
	guint64 size;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	/* TRANSLATORS: performing action */
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
		g_set_error (error, 1, 0, "specify a package name");
		goto out;
	}
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve (store_array, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	if (array->len == 0) {
		g_set_error (error, 1, 0, "no package found");
		goto out;
	}

	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (priv->state, array->len + 1);

	zif_progress_bar_end (priv->progressbar);

	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);

		state_loop = zif_state_get_child (priv->state);
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
		ret = zif_state_done (priv->state, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

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
 * zif_cmd_get_files:
 **/
static gboolean
zif_cmd_get_files (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GPtrArray *files = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a package name");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Get file data"));

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

	/* resolve */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve (store_array, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* at least one result */
	if (array->len > 0) {
		package = g_ptr_array_index (array, 0);
		state_local = zif_state_get_child (priv->state);
		files = zif_package_get_files (package, state_local, error);
		if (files == NULL) {
			ret = FALSE;
			goto out;
		}
		for (i=0; i<files->len; i++)
			g_print ("%s\n", (const gchar *) g_ptr_array_index (files, i));
		g_ptr_array_unref (files);
	} else {
		g_set_error (error, 1, 0, "Failed to match any packages to '%s'", values[0]);
	}

	zif_progress_bar_end (priv->progressbar);

	/* success */
	ret = TRUE;
out:
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

	/* TRANSLATORS: performing action */
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
 * zif_cmd_get_updates:
 **/
static gboolean
zif_cmd_get_updates (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	gchar **search = NULL;
	gint val;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp;
	GPtrArray *store_array = NULL;
	GPtrArray *updates_available = NULL;
	GPtrArray *updates = NULL;
	guint i;
	guint j;
	ZifDepend *depend;
	ZifPackage *package;
	ZifPackage *update;
	ZifState *state_local;
	ZifState *state_loop;
	ZifStore *store_local = NULL;
	ZifTransaction *transaction = NULL;

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Getting updates"));

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (priv->state,
				   error,
				   2, /* add remote */
				   5, /* get local packages */
				   3, /* filter newest */
				   10, /* resolve local list to remote */
				   10, /* add obsoletes */
				   70, /* filter out anything not newer */
//				   5, /* add packages to update queue */
//				   25, /* resolve updates */
				   -1);
	if (!ret)
		goto out;

	/* add remote stores to array */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* setup transaction */
	transaction = zif_transaction_new ();
	store_local = zif_store_local_new ();
	zif_transaction_set_skip_broken (transaction, priv->skip_broken);
	zif_transaction_set_store_local (transaction, store_local);
	zif_transaction_set_stores_remote (transaction, store_array);

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* get packages */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_get_packages (store_local, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* remove any packages that are not newest (think kernel) */
	zif_package_array_filter_newest (array);

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* resolve each one remote */
	search = g_new0 (gchar *, array->len + 1);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		search[i] = g_strdup (zif_package_get_name (package));
	}
	state_local = zif_state_get_child (priv->state);
	updates = zif_store_array_resolve (store_array, search, state_local, error);
	if (updates == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* some repos contain lots of versions of one package */
	zif_package_array_filter_newest (updates);

	/* find each one in a remote repo */
	updates_available = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array->len; i++) {
		package = ZIF_PACKAGE (g_ptr_array_index (array, i));

		/* find updates */
		for (j=0; j<updates->len; j++) {
			update = ZIF_PACKAGE (g_ptr_array_index (updates, j));

			/* newer? */
			val = zif_package_compare (update, package);
			if (val == G_MAXINT)
				continue;
			if (val > 0) {
				g_debug ("*** update %s from %s to %s",
					 zif_package_get_name (package),
					 zif_package_get_version (package),
					 zif_package_get_version (update));
				g_ptr_array_add (updates_available, g_object_ref (update));
				break;
			}
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* add obsoletes */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, array->len);
	for (i=0; i<array->len; i++) {
		package = ZIF_PACKAGE (g_ptr_array_index (array, i));
		depend = zif_depend_new ();
		zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_ANY);
		zif_depend_set_name (depend, zif_package_get_name (package));

		/* find if anything obsoletes this */
		state_loop = zif_state_get_child (state_local);
		array_tmp = zif_store_array_what_obsoletes (store_array, depend, state_loop, error);
		if (array_tmp == NULL)
			goto out;
		for (j=0; j<array_tmp->len; j++) {
			update = ZIF_PACKAGE (g_ptr_array_index (array_tmp, j));
			g_debug ("*** obsolete %s to %s",
				 zif_package_get_name (package),
				 zif_package_get_name (update));
			g_ptr_array_add (updates_available, g_object_ref (update));
		}

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;

		g_object_unref (depend);
		g_ptr_array_unref (array_tmp);
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;
#if 0
	/* add each package as an update */
	g_debug ("adding %i packages", updates_available->len);
	for (i=0; i<updates_available->len; i++) {
		package = g_ptr_array_index (updates_available, i);
		ret = zif_transaction_add_update (transaction, package, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* resolve */
	state_local = zif_state_get_child (priv->state);
	ret = zif_transaction_resolve (transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* print what's going to happen */
	g_print ("%s\n", _("Transaction summary:"));
	for (i=0; i<ZIF_TRANSACTION_REASON_LAST; i++) {
		array_tmp = zif_transaction_get_array_for_reason (transaction, i);
		if (array_tmp->len > 0)
			g_print ("  %s:\n", zif_transaction_reason_to_string (i));
		for (j=0; j<array_tmp->len; j++) {
			package = g_ptr_array_index (array_tmp, j);
			g_print ("  %i.\t%s\n", j+1, zif_package_get_id (package));
		}
		g_ptr_array_unref (array_tmp);
	}
#else
	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (updates_available);
#endif

#if 0

	/* get update details */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (priv->state, array->len);
	for (i=0; i<array->len; i++) {
		ZifUpdate *update;
		ZifUpdateInfo *info;
		ZifChangeset *changeset;
		GPtrArray *update_infos;
		GPtrArray *changelog;
		guint j;

		package = g_ptr_array_index (array, i);
		state_loop = zif_state_get_child (priv->state);
		update = zif_package_remote_get_update_detail (ZIF_PACKAGE_REMOTE (package), state_loop, error);
		if (update == NULL) {
			g_set_error (error, 1, 0, "failed to get update detail for %s: %s",
				 zif_package_get_id (package), error->message);
			g_clear_error (error);

			/* non-fatal */
			ret = zif_state_finished (priv->state_loop, error);
			if (!ret)
				goto out;
			ret = zif_state_done (priv->state, error);
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
		ret = zif_state_done (priv->state, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_print_packages (array);

#endif
	/* success */
	ret = TRUE;
out:
	g_strfreev (search);
	if (store_local != NULL)
		g_object_unref (store_local);
	if (transaction != NULL)
		g_object_unref (transaction);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (updates != NULL)
		g_ptr_array_unref (updates);
	if (updates_available != NULL)
		g_ptr_array_unref (updates_available);
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
	guint version;
	ZifUpgrade *upgrade;

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Getting upgrades"));

	version = zif_config_get_uint (priv->config, "releasever", NULL);
	array = zif_release_get_upgrades_new (priv->release, version, priv->state, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* done with the bar */
	zif_progress_bar_end (priv->progressbar);

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

	/* success */
	ret = TRUE;
out:
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

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a config key");
		goto out;
	}

	/* get value */
	value = zif_config_get_string (priv->config, values[0], NULL);
	if (value == NULL) {
		g_set_error (error, 1, 0, "no value for %s", values[0]);
		goto out;
	}

	/* print the results */
	g_print ("%s = '%s':\n", values[0], value);

	/* success */
	ret = TRUE;
out:
	g_free (value);
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
 * zif_transaction_run:
 **/
static gboolean
zif_cmd_prompt (const gchar *title)
{
	gchar input;

	g_print ("\n%s [y/N] ", title);

	input = getchar ();
	if (input == 'y' || input == 'Y')
		return TRUE;
	return FALSE;
}

/**
 * zif_transaction_run:
 **/
static gboolean
zif_transaction_run (ZifCmdPrivate *priv, ZifTransaction *transaction, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array_tmp;
	GPtrArray *store_array_remote = NULL;
	guint i, j;
	ZifPackage *package;
	ZifState *state_local;
	ZifStore *store_local = NULL;

	/* setup steps */
	ret = zif_state_set_steps (state,
				   error,
				   1, /* add remote stores */
				   30, /* resolve */
				   30, /* prepare */
				   39, /* commit */
				   -1);
	if (!ret)
		goto out;

	/* get remote enabled stores */
	store_array_remote = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_remote_enabled (store_array_remote, state_local, &error_local);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to add remote: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* set local store */
	store_local = zif_store_local_new ();
	zif_transaction_set_store_local (transaction, store_local);
	zif_transaction_set_skip_broken (transaction, priv->skip_broken);

	/* add remote stores */
	zif_transaction_set_stores_remote (transaction, store_array_remote);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* resolve */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_resolve (transaction, state_local, &error_local);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to resolve update: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* print what's going to happen */
	g_print ("%s\n", _("Transaction summary:"));
	for (i=0; i<ZIF_TRANSACTION_REASON_LAST; i++) {
		array_tmp = zif_transaction_get_array_for_reason (transaction, i);
		if (array_tmp->len > 0)
			g_print ("  %s:\n", zif_transaction_reason_to_string (i));
		for (j=0; j<array_tmp->len; j++) {
			package = g_ptr_array_index (array_tmp, j);
			g_print ("  %i.\t%s\n", j+1, zif_package_get_id (package));
		}
		g_ptr_array_unref (array_tmp);
	}

	/* confirm */
	if (!priv->assume_yes && !zif_cmd_prompt (_("Run transaction?"))) {
		ret = FALSE;
		g_set_error (error, 1, 0, "User declined action");
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* prepare */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_prepare (transaction, state_local, &error_local);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to prepare transaction: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* commit */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_commit (transaction, state_local, &error_local);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to commit transaction: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (store_local != NULL)
		g_object_unref (store_local);
	if (store_array_remote != NULL)
		g_ptr_array_unref (store_array_remote);
	return ret;
}

/**
 * zif_cmd_install:
 **/
static gboolean
zif_cmd_install (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	guint i;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *store_array_local = NULL;
	GPtrArray *store_array = NULL;
	GPtrArray *store_array_remote = NULL;
	ZifPackage *package;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a package name");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Installing"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   1, /* add local */
				   23, /* resolve */
				   1, /* add remote */
				   45, /* find remote */
				   30, /* run transaction */
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
	array = zif_store_array_resolve (store_array_local, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len > 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "package already installed");
		goto out;
	}

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
	array = zif_store_array_resolve (store_array_remote, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "could not find package in remote source");
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* install these packages */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		transaction = zif_transaction_new ();
		ret = zif_transaction_add_install (transaction, package, &error_local);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to add install %s: %s",
				 zif_package_get_name (package),
				 error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

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
	if (store_array_remote != NULL)
		g_ptr_array_unref (store_array_remote);

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
	zif_progress_bar_start (priv->progressbar, _("Installing a local file"));
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
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_transaction_add_install (transaction, package, error);
		if (!ret)
			goto out;
	}

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
	zif_progress_bar_start (priv->progressbar, _("Checking manifest files"));

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
	ZifStore *store_local = NULL;

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
	zif_progress_bar_start (priv->progressbar, _("Dumping manifest to file"));

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

	store_local = zif_store_local_new ();
	state_local = zif_state_get_child (priv->state);
	array_local = zif_store_get_packages (store_local, state_local, error);
	if (array_local == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* save to file */
	string = g_string_new ("[Zif Manifest]\n");
	g_string_append (string, "AddLocal=");
	for (i=0; i<array_local->len; i++) {
		package = g_ptr_array_index (array_local, i);
		g_string_append (string, zif_package_get_id (package));
		g_string_append (string, ",");
	}
	g_string_set_size (string, string->len - 1);
	g_string_append (string, "\n");
	g_string_append (string, "AddRemote=");
	for (i=0; i<array_remote->len; i++) {
		package = g_ptr_array_index (array_remote, i);
		g_string_append (string, zif_package_get_id (package));
		g_string_append (string, ",");
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
	if (store_local != NULL)
		g_object_unref (store_local);
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
	ret = zif_store_array_refresh (store_array, TRUE, state_local, error);
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
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *store_array_local = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a package name");
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
	ret = zif_store_array_add_local (store_array_local, state_local, &error_local);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check not already installed */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve (store_array_local, values, state_local, &error_local);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "package not installed");
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* remove these packages */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		transaction = zif_transaction_new ();
		ret = zif_transaction_add_remove (transaction, package, &error_local);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to add remove %s: %s",
				 zif_package_get_name (package),
				 error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

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
	GPtrArray *store_array = NULL;
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
	ret = zif_store_remote_set_enabled (store_remote, FALSE, error);
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
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
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
	GPtrArray *array = NULL;
	GPtrArray *store_array = NULL;
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
	ret = zif_store_remote_set_enabled (store_remote, TRUE, error);
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
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
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
		g_set_error (error, 1, 0, "specify a package name");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Resolving"));

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
	array = zif_store_array_resolve (store_array, values, state_local, error);
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
		g_set_error (error, 1, 0, "specify a category");
		goto out;
	}

	/* TRANSLATORS: performing action */
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
		g_set_error (error, 1, 0, "specify a search term");
		goto out;
	}

	/* TRANSLATORS: performing action */
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
		g_set_error (error, 1, 0, "specify a filename");
		goto out;
	}

	/* TRANSLATORS: performing action */
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
		g_set_error (error, 1, 0, "specify a search term");
		goto out;
	}

	/* TRANSLATORS: performing action */
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
		g_set_error (error, 1, 0, "specify a search term");
		goto out;
	}

	/* TRANSLATORS: performing action */
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

	zif_print_packages (array);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_cmd_update:
 **/
static gboolean
zif_cmd_update (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *store_array_local = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifTransaction *transaction = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a package name");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Updating"));

	/* setup state */
	ret = zif_state_set_steps (priv->state,
				   error,
				   80,
				   10,
				   10,
				   -1);
	if (!ret)
		goto out;

	/* add local store */
	store_array_local = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_local (store_array_local, state_local, &error_local);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to add local store: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* check not already installed */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve (store_array_local, values, state_local, &error_local);
	if (array == NULL) {
		ret = FALSE;
		g_set_error (error, 1, 0, "failed to get results: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "package not installed");
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* install this package */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		transaction = zif_transaction_new ();
		ret = zif_transaction_add_update (transaction, package, &error_local);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to add update %s: %s",
				 zif_package_get_name (package),
				 error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

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
 * zif_cmd_upgrade:
 **/
static gboolean
zif_cmd_upgrade (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	gchar **distro_id_split = NULL;
	guint version;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a distro name, e.g. 'fedora-9'\n");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Upgrading"));

	/* check valid */
	distro_id_split = g_strsplit (values[0], "-", -1);
	if (g_strv_length (distro_id_split) != 2) {
		g_set_error (error, 1, 0, "distribution id invalid");
		goto out;
	}

	/* check fedora */
	if (g_strcmp0 (distro_id_split[0], "fedora") != 0) {
		g_set_error (error, 1, 0, "only 'fedora' is supported");
		goto out;
	}

	/* check release */
	version = atoi (distro_id_split[1]);
	if (version < 13 || version > 99) {
		g_set_error (error, 1, 0, "version number %i is invalid", version);
		goto out;
	}

	/* do the upgrade */
	ret = zif_release_upgrade_version (priv->release, version,
					   ZIF_RELEASE_UPGRADE_KIND_DEFAULT,
					   priv->state, error);
	if (!ret)
		goto out;

	/* clean up after ourselves */
//	g_unlink ("/boot/upgrade/vmlinuz");
//	g_unlink ("/boot/upgrade/initrd.img");

	zif_progress_bar_end (priv->progressbar);

out:
	g_strfreev (distro_id_split);
	return ret;
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
	ZifDepend *depend = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a search term");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Conflicts"));

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

	/* parse the depend */
	depend = zif_depend_new ();
	ret = zif_depend_parse_description (depend, values[0], error);
	if (!ret)
		goto out;
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_what_obsoletes (store_array, depend, state_local, error);
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
	if (depend != NULL)
		g_object_unref (depend);
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
	ZifDepend *depend = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a search term");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Obsoletes"));

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

	/* parse the depend */
	depend = zif_depend_new ();
	ret = zif_depend_parse_description (depend, values[0], error);
	if (!ret)
		goto out;
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_what_obsoletes (store_array, depend, state_local, error);
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
	if (depend != NULL)
		g_object_unref (depend);
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
	ZifDepend *depend = NULL;
	ZifState *state_local;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		g_set_error (error, 1, 0, "specify a search term");
		goto out;
	}

	/* TRANSLATORS: performing action */
	zif_progress_bar_start (priv->progressbar, _("Provides"));

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

	/* parse the depend */
	depend = zif_depend_new ();
	ret = zif_depend_parse_description (depend, values[0], error);
	if (!ret)
		goto out;
	g_object_unref (depend);
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_what_provides (store_array, depend, state_local, error);
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
	if (depend != NULL)
		g_object_unref (depend);
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
	g_string_append_printf (string, "command '%s' not found, valid commands are:\n", command);
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
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean offline = FALSE;
	gboolean profile = FALSE;
	gboolean ret;
	gboolean skip_broken = FALSE;
	gboolean verbose = FALSE;
	gboolean assume_yes = FALSE;
	gchar *cmd_descriptions = NULL;
	gchar *config_file = NULL;
	gchar *http_proxy = NULL;
	gchar *options_help = NULL;
	gchar *root = NULL;
	GError *error = NULL;
	gint retval = 0;
	guint age = 0;
	guint i;
	guint pid;
	guint uid;
	ZifCmdPrivate *priv;
	ZifLock *lock = NULL;
	ZifState *state = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "profile", '\0', 0, G_OPTION_ARG_NONE, &profile,
			_("Enable low level profiling of Zif"), NULL },
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
		{ "skip-broken", 's', 0, G_OPTION_ARG_NONE, &skip_broken,
			_("Skip broken dependencies rather than failing"), NULL },
		{ "assume-yes", 'y', 0, G_OPTION_ARG_NONE, &assume_yes,
			_("Assume yes to all questions"), NULL },
		{ NULL}
	};

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
	zif_progress_bar_set_on_console (priv->progressbar, !verbose);
	zif_progress_bar_set_padding (priv->progressbar, 30);
	zif_progress_bar_set_size (priv->progressbar, 30);

	/* save in the private data */
	priv->skip_broken = skip_broken;
	priv->assume_yes = assume_yes;

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
		g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
		g_log_set_handler ("Zif", G_LOG_LEVEL_DEBUG,
				   zif_log_ignore_cb, NULL);
	}

	/* fallback */
	if (config_file == NULL)
		config_file = g_strdup ("/etc/yum.conf");
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

	/* are we allowed to access the repos */
	if (!offline)
		zif_config_set_boolean (priv->config, "network", TRUE, NULL);

	/* set the maximum age of the repo data */
	if (age > 0)
		zif_config_set_uint (priv->config, "max-age", age, NULL);

	/* are we root? */
	uid = getuid ();
	if (uid != 0) {
		/* TRANSLATORS: we can't run as the user */
		g_print ("%s", _("This program has to be run as the root user."));
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

	/* ZifRelease */
	priv->release = zif_release_new ();
	zif_release_set_boot_dir (priv->release, "/boot/upgrade");
	zif_release_set_cache_dir (priv->release, "/var/cache/PackageKit");
	zif_release_set_repo_dir (priv->release, "/var/cache/yum/preupgrade");
	zif_release_set_uri (priv->release, "http://mirrors.fedoraproject.org/releases.txt");

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

	/* for the signal handler */
	_state = state;

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
		     "get-depends",
		     /* TRANSLATORS: command description */
		     _("List a package's dependencies"),
		     zif_cmd_get_depends);
	zif_cmd_add (priv->cmd_array,
		     "get-details",
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
		     "get-packages",
		     /* TRANSLATORS: command description */
		     _("List all packages"),
		     zif_cmd_get_packages);
	zif_cmd_add (priv->cmd_array,
		     "get-updates",
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
		     "local-install",
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
		     "refresh-cache",
		     /* TRANSLATORS: command description */
		     _("Generate the metadata cache"),
		     zif_cmd_refresh_cache);
	zif_cmd_add (priv->cmd_array,
		     "remove",
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
		     "repo-list",
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
		     "search-details",
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
		     "update",
		     /* TRANSLATORS: command description */
		     _("Update a package to the newest available version"),
		     zif_cmd_update);
	zif_cmd_add (priv->cmd_array,
		     "upgrade",
		     /* TRANSLATORS: command description */
		     _("Upgrade the operating system to a newer version"),
		     zif_cmd_upgrade);
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
		     "what-provides",
		     /* TRANSLATORS: command description */
		     _("Find what package provides the given value"),
		     zif_cmd_what_provides);

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
	zif_progress_bar_end (priv->progressbar);
	if (!ret) {
		/* TODO: translate errors */
		g_print ("Failed: %s\n", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}
out:
	if (priv != NULL) {
		g_object_unref (priv->progressbar);
		if (priv->config != NULL)
			g_object_unref (priv->config);
		if (priv->release != NULL)
			g_object_unref (priv->release);
		if (priv->state != NULL)
			g_object_unref (priv->state);
		if (priv->cmd_array != NULL)
			g_ptr_array_unref (priv->cmd_array);
		g_option_context_free (priv->context);
	}
	if (lock != NULL) {
		GError *error_local = NULL;
		ret = zif_lock_set_unlocked (lock, &error_local);
		if (!ret) {
			g_warning ("failed to unlock: %s", error_local->message);
			g_error_free (error_local);
		}
		g_object_unref (lock);
	}

	/* free state */
	g_free (root);
	g_free (cmd_descriptions);
	g_free (http_proxy);
	g_free (config_file);
	g_free (options_help);
	return retval;
}

