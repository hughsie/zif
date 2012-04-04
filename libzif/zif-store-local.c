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

/**
 * SECTION:zif-store-local
 * @short_description: Store for installed packages
 *
 * A #ZifStoreLocal is a subclassed #ZifStore and operates on installed objects.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>

#include <glib.h>
#include <signal.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmts.h>
#include <fcntl.h>

#include "zif-config.h"
#include "zif-history.h"
#include "zif-monitor.h"
#include "zif-package-local.h"
#include "zif-package-private.h"
#include "zif-state-private.h"
#include "zif-store-local.h"
#include "zif-utils-private.h"

#define ZIF_STORE_LOCAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_LOCAL, ZifStoreLocalPrivate))

struct _ZifStoreLocalPrivate
{
	gchar			*prefix;
	ZifMonitor		*monitor;
	ZifConfig		*config;
	guint			 monitor_changed_id;
};

G_DEFINE_TYPE (ZifStoreLocal, zif_store_local, ZIF_TYPE_STORE)
static gpointer zif_store_local_object = NULL;

/**
 * zif_store_local_set_prefix:
 * @store: A #ZifStoreLocal
 * @prefix: The install root, e.g. "/", or NULL to use the default
 * @error: A #GError, or %NULL
 *
 * Sets the prefix to use for the install root.
 *
 * Using @prefix set to %NULL to use the value from the config file
 * has been supported since 0.1.3. Earlier versions will assert.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_local_set_prefix (ZifStoreLocal *store, const gchar *prefix, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *filename = NULL;
	gchar *prefix_real = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get from config file */
	if (prefix == NULL) {
		prefix_real = zif_config_get_string (store->priv->config,
						     "prefix",
						     &error_local);
		if (prefix_real == NULL) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "default prefix not available: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	} else {
		prefix_real = g_strdup (prefix);
	}

	/* check prefix is canonical */
	ret = g_str_has_prefix (prefix_real, "/");
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "prefix %s not canonical (leading slash)",
			     prefix_real);
		goto out;
	}

	/* check file exists */
	ret = g_file_test (prefix_real, G_FILE_TEST_IS_DIR);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "prefix %s does not exist", prefix_real);
		goto out;
	}

	/* is the same */
	if (g_strcmp0 (prefix_real, store->priv->prefix) == 0)
		goto out;

	/* empty cache */
	g_debug ("abandoning cache");
	zif_store_unload (ZIF_STORE (store), NULL);

	/* setup watch */
	filename = g_build_filename (prefix_real, "var", "lib", "rpm", "Packages", NULL);
	ret = zif_monitor_add_watch (store->priv->monitor, filename, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to setup watch: %s",
				     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* save new value */
	g_free (store->priv->prefix);
	store->priv->prefix = g_strdup (prefix_real);
out:
	g_free (prefix_real);
	g_free (filename);
	return ret;
}

/**
 * zif_store_local_get_prefix:
 * @store: A #ZifStoreLocal
 *
 * Gets the prefix to use for the install root.
 *
 * Return value: The install prefix, e.g. "/"
 *
 * Since: 0.1.3
 **/
const gchar *
zif_store_local_get_prefix (ZifStoreLocal *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	return store->priv->prefix;
}

/**
 * zif_store_local_set_releasever:
 **/
static gboolean
zif_store_local_set_releasever (ZifStoreLocal *store,
				ZifState *state,
				GError **error)
{
	gboolean ret = FALSE;
	gchar *releasever = NULL;
	gchar *releasever_pkg;
	gchar **version_split = NULL;
	GPtrArray *depends = NULL;
	GPtrArray *packages = NULL;
	guint release_ver_type;
	ZifDepend *depend;
	ZifPackage *package_tmp;

	/* get the package name of the provide */
	releasever_pkg = zif_config_get_string (store->priv->config,
						"releasever_pkg",
						error);
	if (releasever_pkg == NULL)
		goto out;

	/* get the thing that provides the releasever_pkg */
	depends = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	depend = zif_depend_new ();
	ret = zif_depend_parse_description (depend,
					    releasever_pkg,
					    error);
	if (!ret)
		goto out;
	g_ptr_array_add (depends, depend);
	packages = zif_store_what_provides (ZIF_STORE (store),
					    depends,
					    state,
					    error);
	if (packages == NULL) {
		ret = FALSE;
		goto out;
	}

	/* invalid */
	if (packages->len == 0) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_NO_RELEASEVER,
			     "nothing installed provides %s",
			     releasever_pkg);
		goto out;
	}

	/* parse the package version */
	package_tmp = g_ptr_array_index (packages, 0);
	version_split = g_strsplit_set (zif_package_get_version (package_tmp),
					":-", 3);

	/* epoch:version-release */
	release_ver_type = g_strv_length (version_split);
	if (release_ver_type == 3) {
		releasever = g_strdup (version_split[1]);
	} else if (release_ver_type == 2) {
		releasever = g_strdup (version_split[0]);
	} else {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_NO_RELEASEVER,
			     "unexpected release version format %s",
			     zif_package_get_version (package_tmp));
		goto out;
	}

	/* set the releasever */
	g_debug ("setting releasever '%s'", releasever);
	ret = zif_config_set_string (store->priv->config,
				     "releasever",
				     releasever,
				     error);
	if (!ret)
		goto out;
out:
	g_free (releasever);
	g_free (releasever_pkg);
	g_strfreev (version_split);
	if (depends != NULL)
		g_ptr_array_unref (depends);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	return ret;
}

/**
 * zif_store_local_load:
 **/
static gboolean
zif_store_local_load (ZifStore *store, ZifState *state, GError **error)
{
	gboolean ret = TRUE;
	gboolean use_installed_history;
	gboolean yumdb_allow_read;
	GError *error_local = NULL;
	gint rc;
	guint existing_releasever;
	Header header;
	rpmdbMatchIterator mi = NULL;
	rpmts ts = NULL;
	ZifHistory *history = NULL;
	ZifPackageCompareMode compare_mode;
	ZifPackageLocalFlags flags = 0;
	ZifPackage *package;
	ZifState *state_local;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* setup steps */
	if (local->priv->prefix == NULL) {
		ret = zif_state_set_steps (state,
					   error,
					   5, /* set prefix */
					   80, /* add packages */
					   15, /* set releasever */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   90, /* add packages */
					   10, /* set releasever */
					   -1);
	}
	if (!ret)
		goto out;

	/* use default prefix */
	if (local->priv->prefix == NULL) {

		/* set prefix */
		ret = zif_store_local_set_prefix (local, NULL, error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	zif_state_set_allow_cancel (state, FALSE);
	zif_state_action_start (state,
				ZIF_STATE_ACTION_LOADING_RPMDB,
				local->priv->prefix);

	/* lookup in yumdb */
	yumdb_allow_read = zif_config_get_boolean (local->priv->config,
						   "yumdb_allow_read",
						   NULL);
	if (yumdb_allow_read) {
		g_debug ("using yumdb origin lookup");
		flags += ZIF_PACKAGE_LOCAL_FLAG_USE_YUMDB;
	} else {
		g_debug ("not using yumdb lookup as disabled");
	}

	/* get the compare mode */
	compare_mode = zif_config_get_enum (local->priv->config,
					    "pkg_compare_mode",
					    zif_package_compare_mode_from_string,
					    error);
	if (compare_mode == G_MAXUINT) {
		ret = FALSE;
		goto out;
	}

	/* get list */
	ts = rpmtsCreate ();
	rc = rpmtsSetRootDir (ts, local->priv->prefix);
	if (rc < 0) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "failed to set root (%s)",
			     local->priv->prefix);
		goto out;
	}
	g_debug ("using rpmdb at %s", local->priv->prefix);
	mi = rpmtsInitIterator (ts, RPMDBI_PACKAGES, NULL, 0);
	if (mi == NULL)
		g_warning ("failed to get iterator");

	/* undo librpms attempt to steal SIGINT, and instead fail
	 * the transaction in a nice way */
	zif_state_cancel_on_signal (state, SIGINT);

	/* we don't know how many packages there are */
	state_local = zif_state_get_child (state);
	zif_state_set_report_progress (state_local, FALSE);

	/* add each package from the rpmdb */
	do {
		header = rpmdbNextIterator (mi);
		if (header == NULL)
			break;
		package = zif_package_local_new ();
		zif_package_set_installed (package, TRUE);
		ret = zif_package_local_set_from_header (ZIF_PACKAGE_LOCAL (package),
							 header,
							 flags,
							 &error_local);
		if (!ret) {
			/* we ignore this one */
			if (error_local->domain == ZIF_PACKAGE_ERROR &&
			    error_local->code == ZIF_PACKAGE_ERROR_NO_SUPPORT) {
				g_clear_error (&error_local);
				g_object_unref (package);
			} else {
				g_set_error (error,
					     ZIF_STORE_ERROR,
					     ZIF_STORE_ERROR_FAILED,
						     "failed to set from header: %s",
					     error_local->message);
				g_error_free (error_local);
				g_object_unref (package);
				goto out;
			}
		} else {
			zif_package_set_compare_mode (package,
						      compare_mode);
			zif_store_add_package (store, package, NULL);
			g_object_unref (package);
		}

		/* check cancelled (okay to reuse as we called
		 * zif_state_set_report_progress before)*/
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	} while (TRUE);

	/* turn checks back on */
	zif_state_set_report_progress (state_local, TRUE);
	ret = zif_state_finished (state_local, error);
	if (!ret)
		goto out;

	/* lookup in history database */
	use_installed_history = zif_config_get_boolean (local->priv->config,
							"use_installed_history",
							NULL);
	if (use_installed_history) {
		g_debug ("using history lookup");

		/* we have to force this here, otherwise
		 * zif_store_find_package() starts klass->load() again */
		g_object_set (store, "loaded", TRUE, NULL);

		/* do all the packages in one pass */
		history = zif_history_new ();
		ret = zif_history_set_repo_for_store (history,
						      store,
						      &error_local);
		if (!ret) {
			if (error_local->domain == ZIF_HISTORY_ERROR &&
			    error_local->code == ZIF_HISTORY_ERROR_FAILED_TO_OPEN) {
				g_debug ("no history lookup avilable: %s",
					 error_local->message);
				g_clear_error (&error_local);
			} else {
				g_propagate_error (error, error_local);
				goto out;
			}
		}
	} else {
		g_debug ("not using history lookup as disabled");
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* set releasever if not already set */
	existing_releasever = zif_config_get_uint (local->priv->config,
						   "releasever",
						   NULL);
	if (existing_releasever == G_MAXUINT) {
		g_object_set (store, "loaded", TRUE, NULL);
		state_local = zif_state_get_child (state);
		ret = zif_store_local_set_releasever (local,
						      state_local,
						      error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (history != NULL)
		g_object_unref (history);
	if (mi != NULL)
		rpmdbFreeIterator (mi);
	if (ts != NULL)
		rpmtsFree(ts);

	/* cleanup, and make SIGINT do something sane */
	zif_state_cancel_on_signal (state, SIGINT);

	return ret;
}

/**
 * zif_store_local_get_id:
 **/
static const gchar *
zif_store_local_get_id (ZifStore *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	return "installed";
}

/**
 * zif_store_local_file_monitor_cb:
 **/
static void
zif_store_local_file_monitor_cb (ZifMonitor *monitor, ZifStore *store)
{
	g_debug ("rpmdb changed");
	zif_store_unload (store, NULL);
}

/**
 * zif_store_local_finalize:
 **/
static void
zif_store_local_finalize (GObject *object)
{
	ZifStoreLocal *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_LOCAL (object));
	store = ZIF_STORE_LOCAL (object);

	g_signal_handler_disconnect (store->priv->monitor, store->priv->monitor_changed_id);
	g_object_unref (store->priv->monitor);
	g_object_unref (store->priv->config);
	g_free (store->priv->prefix);

	G_OBJECT_CLASS (zif_store_local_parent_class)->finalize (object);
}

/**
 * zif_store_local_class_init:
 **/
static void
zif_store_local_class_init (ZifStoreLocalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifStoreClass *store_class = ZIF_STORE_CLASS (klass);
	object_class->finalize = zif_store_local_finalize;

	/* map */
	store_class->load = zif_store_local_load;
	store_class->get_id = zif_store_local_get_id;

	g_type_class_add_private (klass, sizeof (ZifStoreLocalPrivate));
}

/**
 * zif_store_local_init:
 **/
static void
zif_store_local_init (ZifStoreLocal *store)
{
	store->priv = ZIF_STORE_LOCAL_GET_PRIVATE (store);
	store->priv->monitor = zif_monitor_new ();
	store->priv->config = zif_config_new ();
	store->priv->monitor_changed_id =
		g_signal_connect (store->priv->monitor, "changed",
				  G_CALLBACK (zif_store_local_file_monitor_cb), store);

	/* make sure initialized */
	zif_init ();
}

/**
 * zif_store_local_new:
 *
 * Return value: A new #ZifStoreLocal instance.
 *
 * Since: 0.1.0
 **/
ZifStore *
zif_store_local_new (void)
{
	if (zif_store_local_object != NULL) {
		g_object_ref (zif_store_local_object);
	} else {
		zif_store_local_object = g_object_new (ZIF_TYPE_STORE_LOCAL, NULL);
		g_object_add_weak_pointer (zif_store_local_object, &zif_store_local_object);
	}
	return ZIF_STORE (zif_store_local_object);
}

