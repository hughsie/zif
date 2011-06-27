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
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmts.h>
#include <fcntl.h>

#include "zif-config.h"
#include "zif-monitor.h"
#include "zif-package-local.h"
#include "zif-store-local.h"

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
 * zif_store_local_load:
 **/
static gboolean
zif_store_local_load (ZifStore *store, ZifState *state, GError **error)
{
	gboolean ret = TRUE;
	gboolean yumdb_allow_read;
	GError *error_local = NULL;
	gint rc;
	Header header;
	rpmdbMatchIterator mi = NULL;
	rpmts ts = NULL;
	ZifPackageLocalFlags flags = 0;
	ZifPackage *package;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);
	const gchar *tmp;
	ZifPackageCompareMode compare_mode;

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* setup steps */
	if (local->priv->prefix == NULL) {
		ret = zif_state_set_steps (state,
					   error,
					   10, /* set prefix */
					   90, /* add packages */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   100, /* add packages */
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

	/* lookup in yumdb */
	yumdb_allow_read = zif_config_get_boolean (local->priv->config,
						   "yumdb_allow_read",
						   NULL);
	if (yumdb_allow_read) {
		g_debug ("using yumdb origin lookup");
		flags += ZIF_PACKAGE_LOCAL_FLAG_LOOKUP;
	} else {
		g_debug ("not using yumdb lookup as disabled");
	}

	/* speed up for the future */
//	flags += ZIF_PACKAGE_LOCAL_FLAG_REPAIR;

	/* get the compare mode */
	tmp = zif_config_get_string (local->priv->config,
				     "pkg_compare_mode",
				     error);
	if (tmp == NULL) {
		ret = FALSE;
		goto out;
	}
	compare_mode = zif_package_compare_mode_from_string (tmp);

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
	do {
		header = rpmdbNextIterator (mi);
		if (header == NULL)
			break;
		package = zif_package_local_new ();
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
	} while (TRUE);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (mi != NULL)
		rpmdbFreeIterator (mi);
	if (ts != NULL)
		rpmtsFree(ts);
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

