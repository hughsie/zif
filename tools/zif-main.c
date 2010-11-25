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
#include <termios.h>

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
	const gchar *trusted_str = "";
	gchar *padding_str;
	ZifState *state_tmp;
	gchar **split;
	ZifPackageTrustKind trust;

	package_id = zif_package_get_id (package);
	split = zif_package_id_split (package_id);
	state_tmp = zif_state_new ();
	summary = zif_package_get_summary (package, state_tmp, NULL);
	if (padding > 0) {
		padding_str = g_strnfill (padding - strlen (package_id), ' ');
	} else {
		padding_str = g_strnfill (2, ' ');
	}
	trust = zif_package_get_trust_kind (package);
	if (trust == ZIF_PACKAGE_TRUST_KIND_PUBKEY) {
		trusted_str = _("[⚐]");
	} else if (trust == ZIF_PACKAGE_TRUST_KIND_NONE) {
		trusted_str = _("[⚑]");
	}
	g_print ("%s-%s.%s (%s) %s%s%s\n",
		 split[ZIF_PACKAGE_ID_NAME],
		 split[ZIF_PACKAGE_ID_VERSION],
		 split[ZIF_PACKAGE_ID_ARCH],
		 split[ZIF_PACKAGE_ID_DATA],
		 trusted_str,
		 padding_str,
		 summary);
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
		/* TRANSLATORS: a file is currently downloading */
		return _("Downloading");
	}
	if (action == ZIF_STATE_ACTION_LOADING_REPOS) {
		/* TRANSLATORS: a repository file is being read, and
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
	return zif_state_action_to_string (action);
}

/**
 * zif_state_action_changed_cb:
 **/
static void
zif_state_action_changed_cb (ZifState *state, ZifStateAction action, const gchar *action_hint, ZifProgressBar *progressbar)
{
	gchar *pretty_hint = NULL;

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

		pretty_hint = g_path_get_basename (action_hint);

	/* show nice name for package */
	} else if (zif_package_id_check (action_hint)) {

		pretty_hint = zif_package_id_get_printable (action_hint);

	/* fallback to just showing it */
	} else {
		pretty_hint = g_strdup (action_hint);
	}
	zif_progress_bar_set_detail (progressbar, pretty_hint);
out:
	g_free (pretty_hint);
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
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, _("No %s package was found"), values[0]);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* TRANSLATORS: downloading packages */
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
	GPtrArray *requires = NULL;
	ZifDepend *require;
	const gchar *require_str;
	GPtrArray *provides = NULL;
	const gchar *package_id;
	guint i, j;
	gchar **split;
	GString *string = NULL;

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message: the user did not specify
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
		package_id = zif_package_get_id (package);
		split = zif_package_id_split (package_id);
		/* TRANSLATORS: this is a item prefix */
		g_string_append_printf (string, "   %s %s-%s.%s (%s)\n",
					_("Provider:"),
					split[ZIF_PACKAGE_ID_NAME],
					split[ZIF_PACKAGE_ID_VERSION],
					split[ZIF_PACKAGE_ID_ARCH],
					split[ZIF_PACKAGE_ID_DATA]);
		g_strfreev (split);
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
		/* TRANSLATORS: error message: the user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a package name"));
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
		/* TRANSLATORS: error message: nothing was found */
		g_set_error_literal (error, 1, 0,
				     _("No package was found"));
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

		/* TRANSLATORS: these are headers for the package data */
		g_print ("%s\t : %s\n", _("Name"), zif_package_get_name (package));
		g_print ("%s\t : %s\n", _("Version"), zif_package_get_version (package));
		g_print ("%s\t : %s\n", _("Arch"), zif_package_get_arch (package));
		g_print ("%s\t : %" G_GUINT64_FORMAT " bytes\n", _("Size"), size);
		g_print ("%s\t : %s\n", _("Repo"), zif_package_get_data (package));
		g_print ("%s\t : %s\n", _("Summary"), summary);
		g_print ("%s\t : %s\n", _("URL"), url);
		g_print ("%s\t : %s\n", _("License"), license);
		g_print ("%s\t : %s\n", _("Description"), description);

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
		/* TRANSLATORS: error message: user needs to specify a value */
		g_set_error (error, 1, 0, _("Specify a package name"));
		goto out;
	}

	/* TRANSLATORS: getting file lists for a package */
	zif_progress_bar_start (priv->progressbar, _("Getting files"));

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
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, "%s %s", _("Failed to match any packages for :"), values[0]);
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
 * zif_cmd_get_updates:
 *
 * Returns an array of the *new* packages, not the things that are
 * going to be updated.
 **/
static GPtrArray *
zif_get_update_array (ZifCmdPrivate *priv, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	gchar **search = NULL;
	gint val;
	GPtrArray *array = NULL;
	GPtrArray *array_obsoletes = NULL;
	GPtrArray *store_array = NULL;
	GPtrArray *updates_available = NULL;
	GPtrArray *updates = NULL;
	GPtrArray *depend_array = NULL;
	guint i;
	guint j;
	ZifDepend *depend;
	ZifPackage *package;
	ZifPackage *update;
	ZifState *state_local;
	ZifStore *store_local = NULL;

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (state,
				   error,
				   2, /* add remote */
				   5, /* get local packages */
				   3, /* filter newest */
				   10, /* resolve local list to remote */
				   10, /* add obsoletes */
				   70, /* filter out anything not newer */
				   -1);
	if (!ret)
		goto out;

	/* add remote stores to array */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get packages */
	state_local = zif_state_get_child (state);
	store_local = zif_store_local_new ();
	array = zif_store_get_packages (store_local, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* remove any packages that are not newest (think kernel) */
	zif_package_array_filter_newest (array);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* resolve each one remote */
	search = g_new0 (gchar *, array->len + 1);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		search[i] = g_strdup (zif_package_get_name (package));
	}
	state_local = zif_state_get_child (state);
	updates = zif_store_array_resolve (store_array, search, state_local, error);
	if (updates == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
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
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* add obsoletes */
	depend_array = zif_object_array_new ();
	for (i=0; i<array->len; i++) {
		package = ZIF_PACKAGE (g_ptr_array_index (array, i));
		depend = zif_depend_new ();
		zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_EQUAL);
		zif_depend_set_name (depend, zif_package_get_name (package));
		zif_depend_set_version (depend, zif_package_get_version (package));
		zif_object_array_add (depend_array, depend);
		g_object_unref (depend);
	}

	/* find if anything obsoletes these */
	state_local = zif_state_get_child (state);
	array_obsoletes = zif_store_array_what_obsoletes (store_array, depend_array, state_local, error);
	if (array_obsoletes == NULL)
		goto out;
	for (j=0; j<array_obsoletes->len; j++) {
		update = ZIF_PACKAGE (g_ptr_array_index (array_obsoletes, j));
		g_debug ("*** obsolete %s",
			 zif_package_get_name (update));
	}

	/* add obsolete array to updates */
	zif_object_array_add_array (updates_available, array_obsoletes);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	ret = TRUE;
out:
	g_strfreev (search);
	if (store_local != NULL)
		g_object_unref (store_local);
	if (depend_array != NULL)
		g_ptr_array_unref (depend_array);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array_obsoletes != NULL)
		g_ptr_array_unref (array_obsoletes);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (updates != NULL)
		g_ptr_array_unref (updates);
	return updates_available;
}

/**
 * zif_cmd_get_updates:
 **/
static gboolean
zif_cmd_get_updates (ZifCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	ZifTransaction *transaction = NULL;

	/* TRANSLATORS: getting the list of packages that can be updated */
	zif_progress_bar_start (priv->progressbar, _("Getting updates"));

	/* get the update list */
	array = zif_get_update_array (priv, priv->state, error);
	if (array == NULL)
		goto out;

#if 0
	/* setup transaction */
	transaction = zif_transaction_new ();
	store_local = zif_store_local_new ();
	zif_transaction_set_store_local (transaction, store_local);
	zif_transaction_set_stores_remote (transaction, store_array);

	/* add each package as an update */
	g_debug ("adding %i packages", array->len);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
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
#else
	zif_progress_bar_end (priv->progressbar);
	zif_print_packages (array);
#endif

	/* success */
	ret = TRUE;
out:
	if (transaction != NULL)
		g_object_unref (transaction);
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
	g_print ("%s\n", _("Distribution upgrades available:"));
	for (i=0; i<array->len; i++) {
		upgrade = g_ptr_array_index (array, i);
		if (!zif_upgrade_get_enabled (upgrade))
			continue;
		g_print ("%s\t[%s]\n",
			 zif_upgrade_get_id (upgrade),
			 zif_upgrade_get_stable (upgrade) ? _("stable") : _("unstable"));
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

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: the user didn't specify a required value */
		g_set_error_literal (error, 1, 0, _("Specify a config key"));
		goto out;
	}

	/* get value */
	value = zif_config_get_string (priv->config, values[0], NULL);
	if (value == NULL) {
		/* TRANSLATORS: there was no value in the config files */
		g_set_error (error, 1, 0, _("No value for %s"), values[0]);
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

	g_print ("%s [y/N] ", title);

	fflush (stdin);
	input = zif_cmd_getchar_unbuffered ();
	g_print ("%c\n", input);
	if (input == 'y' || input == 'Y')
		return TRUE;
	return FALSE;
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
	GPtrArray *array;
	guint i, j;
	gchar *printable;
	ZifPackage *package;

	g_print ("%s\n", _("Transaction summary:"));
	for (i=0; i<ZIF_TRANSACTION_REASON_LAST; i++) {
		array = zif_transaction_get_array_for_reason (transaction, i);
		if (array->len > 0)
			g_print ("  %s:\n", zif_transaction_reason_to_string_localized (i));
		for (j=0; j<array->len; j++) {
			package = g_ptr_array_index (array, j);
			printable = zif_package_id_get_printable (zif_package_get_id (package));
			g_print ("  %i.\t%s\n", j+1, printable);
			g_free (printable);
		}
		g_ptr_array_unref (array);
	}
}

/**
 * zif_transaction_run:
 **/
static gboolean
zif_transaction_run (ZifCmdPrivate *priv, ZifTransaction *transaction, ZifState *state, GError **error)
{
	gboolean assume_yes;
	gboolean ret;
	gboolean untrusted = FALSE;
	GPtrArray *install = NULL;
	GPtrArray *store_array_remote = NULL;
	guint i;
	ZifPackage *package_tmp;
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
	ret = zif_store_array_add_remote_enabled (store_array_remote, state_local, error);
	if (!ret)
		goto out;

	/* set local store */
	store_local = zif_store_local_new ();
	zif_transaction_set_store_local (transaction, store_local);

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

	/* show */
	zif_main_show_transaction (transaction);

	/* confirm */
	if (priv->assume_no) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error_literal (error, 1, 0, _("Automatically declined action"));
		goto out;
	}

	/* ask the question */
	assume_yes = zif_config_get_boolean (priv->config, "assumeyes", NULL);
	if (!assume_yes) {
		zif_progress_bar_end (priv->progressbar);
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
	ret = zif_transaction_commit (transaction, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (store_local != NULL)
		g_object_unref (store_local);
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
		/* TRANSLATORS: error message: the user did not specify
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

	/* check not already installed */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_array_resolve (store_array_local, values, state_local, error);
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
	array = zif_store_array_resolve (store_array_remote, values, state_local, error);
	if (array == NULL) {
		ret = FALSE;
		goto out;
	}
	if (array->len == 0) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, _("Could not find %s in repositories"), values[0]);
		goto out;
	}

	/* we only want the newest version installed */
	zif_package_array_filter_newest (array);

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* install these packages */
	transaction = zif_transaction_new ();
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
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
		/* TRANSLATORS: error message: the user did not specify
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
	array = zif_store_array_resolve (store_array_local, values, state_local, error);
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

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* remove these packages */
	transaction = zif_transaction_new ();
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_transaction_add_remove (transaction, package, error);
		if (!ret)
			goto out;
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
		/* TRANSLATORS: error message: the user did not specify
		 * a required value */
		g_set_error_literal (error, 1, 0,
				     _("Specify a package name"));
		goto out;
	}

	/* TRANSLATORS: finding packages from a name */
	zif_progress_bar_start (priv->progressbar, _("Finding package name"));

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
		/* TRANSLATORS: error message: the user did not specify
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
	zif_progress_bar_start (priv->progressbar, _("Updating everything"));

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
out:
	if (transaction != NULL)
		g_object_unref (transaction);
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
	GPtrArray *array = NULL;
	GPtrArray *store_array_local = NULL;
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
	if (array->len == 0) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, _("The %s package is not installed"), values[0]);
		goto out;
	}

	/* filter down to the newest installed version */
	zif_package_array_filter_newest (array);

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	/* update this package */
	transaction = zif_transaction_new ();
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		ret = zif_transaction_add_update (transaction, package, error);
		if (!ret)
			goto out;
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
	zif_progress_bar_start (priv->progressbar, _("Getting update details"));

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
	ret = zif_store_array_add_remote (store_array_local, state_local, error);
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
	if (array->len == 0) {
		ret = FALSE;
		/* TRANSLATORS: error message */
		g_set_error (error, 1, 0, _("The %s package is not installed"), values[0]);
		goto out;
	}

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
		state_loop = zif_state_get_child (priv->state);
		update = zif_package_remote_get_update_detail (ZIF_PACKAGE_REMOTE (package), state_loop, &error_local);
		if (update == NULL) {
			ret = FALSE;
			g_set_error (error, 1, 0, "failed to get update detail for %s: %s",
				 zif_package_get_id (package), error_local->message);
			g_clear_error (&error_local);
			goto out;
		}

		/* TRANSLATORS: these are headings for the update details */
		g_print ("\t%s\t%s\n", _("kind"), zif_update_state_to_string (zif_update_get_kind (update)));
		g_print ("\t%s\t%s\n", _("id"), zif_update_get_id (update));
		g_print ("\t%s\t%s\n", _("title"), zif_update_get_title (update));
		g_print ("\t%s\t%s\n", _("description"), zif_update_get_description (update));
		g_print ("\t%s\t%s\n", _("issued"), zif_update_get_issued (update));
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
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, error);
	if (!ret)
		goto out;

	zif_progress_bar_end (priv->progressbar);

	/* TODO: print a formatted GString */
	zif_print_packages (array);
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

	/* check we have a value */
	if (values == NULL || values[0] == NULL) {
		/* TRANSLATORS: error message, missing value */
		g_set_error (error, 1, 0, "%s 'fedora-9'\n", _("Specify a distro name, e.g."));
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
					   ZIF_RELEASE_UPGRADE_KIND_DEFAULT,
					   priv->state, error);
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
	depend_array_tmp = zif_object_array_new ();
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
	if (depend_array_tmp != NULL)
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
	array = zif_store_array_what_provides (store_array, depend_array, state_local, error);
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
	ZifDepend *depend = NULL;
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
	array = zif_store_array_what_requires (store_array, depend_array, state_local, error);
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
	ZifStore *store_local = NULL;
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
	store_local = zif_store_local_new ();
	transaction = zif_transaction_new ();
	zif_transaction_set_store_local (transaction, store_local);
	zif_transaction_set_stores_remote (transaction, stores_remote);

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
			ret = zif_transaction_commit (transaction, priv->state, &error_local);
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
				array = zif_store_array_resolve (stores_remote, &split[1], priv->state, &error_local);
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
				package = zif_store_find_package (store_local, split[1], priv->state, &error_local);
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
				array = zif_store_resolve (store_local, &split[1], priv->state, &error_local);
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
				package = zif_store_find_package (store_local, split[1], priv->state, &error_local);
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
				array = zif_store_resolve (store_local, &split[1], priv->state, &error_local);
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
	if (store_local != NULL)
		g_object_unref (store_local);
	if (transaction != NULL)
		g_object_unref (transaction);
	if (stores_remote != NULL)
		g_ptr_array_unref (stores_remote);
	g_timer_destroy (timer);
	g_free (old_buffer);
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
	gboolean assume_no = FALSE;
	gchar *cmd_descriptions = NULL;
	gchar *config_file = NULL;
	gchar *excludes = NULL;
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
		{ "excludes", 'c', 0, G_OPTION_ARG_STRING, &excludes,
			_("Exclude certain packages"), NULL },
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
		{ "assume-no", 'n', 0, G_OPTION_ARG_NONE, &assume_no,
			_("Assume no to all questions"), NULL },
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
	zif_progress_bar_set_padding (priv->progressbar, 30);
	zif_progress_bar_set_size (priv->progressbar, 30);

	/* save in the private data */
	priv->assume_no = assume_no;

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
	ret = zif_config_set_boolean (priv->config, "assumeyes", assume_yes, &error);
	if (!ret) {
		g_error ("failed to set assumeyes: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* are we allowed to access the repos */
	if (!offline)
		zif_config_set_boolean (priv->config, "network", TRUE, NULL);

	/* set the maximum age of the repo data */
	if (age > 0)
		zif_config_set_uint (priv->config, "metadata_expire", age, NULL);

	/* are we root? */
	uid = getuid ();
	if (uid != 0) {
		/* TRANSLATORS: we can't run as the user */
		g_print ("%s\n", _("This program has to be run as the root user."));
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
		     "shell",
		     /* TRANSLATORS: command description */
		     _("Run an interactive shell"),
		     zif_cmd_shell);
	zif_cmd_add (priv->cmd_array,
		     "update",
		     /* TRANSLATORS: command description */
		     _("Update a package to the newest available version"),
		     zif_cmd_update);
	zif_cmd_add (priv->cmd_array,
		     "update-details",
		     /* TRANSLATORS: command description */
		     _("Display details about an update"),
		     zif_cmd_update_details);
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
	zif_cmd_add (priv->cmd_array,
		     "what-requires",
		     /* TRANSLATORS: command description */
		     _("Find what package requires the given value"),
		     zif_cmd_what_requires);

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
		} else if (error->domain == ZIF_MD_ERROR) {
			switch (error->code) {
			case ZIF_MD_ERROR_FAILED:
				/* TRANSLATORS: error message */
				message = _("Failed");
				break;
			case ZIF_MD_ERROR_NO_SUPPORT:
				/* TRANSLATORS: error message */
				message = _("No support");
				break;
			case ZIF_MD_ERROR_FAILED_TO_LOAD:
				/* TRANSLATORS: error message */
				message = _("Failed to load");
				break;
			case ZIF_MD_ERROR_FAILED_AS_OFFLINE:
				/* TRANSLATORS: error message */
				message = _("Failed as offline");
				break;
			case ZIF_MD_ERROR_FAILED_DOWNLOAD:
				/* TRANSLATORS: error message */
				message = _("Failed to download");
				break;
			case ZIF_MD_ERROR_BAD_SQL:
				/* TRANSLATORS: error message */
				message = _("Bad SQL");
				break;
			case ZIF_MD_ERROR_FILE_TOO_OLD:
				/* TRANSLATORS: error message */
				message = _("File is too old");
				break;
			case ZIF_MD_ERROR_NO_FILENAME:
				/* TRANSLATORS: error message */
				message = _("No filename");
				break;
			default:
				/* TRANSLATORS: error message */
				message = _("Unhandled metadata error");
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
		if (priv->config != NULL)
			g_object_unref (priv->config);
		if (priv->state != NULL)
			g_object_unref (priv->state);
		if (priv->cmd_array != NULL)
			g_ptr_array_unref (priv->cmd_array);
		g_option_context_free (priv->context);
		g_free (priv);
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

