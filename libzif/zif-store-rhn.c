/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-store-rhn
 * @short_description: Store for installed packages
 *
 * A #ZifStoreRhn is a subclassed #ZifStore and operates on remote objects.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>
#include <libsoup/soup.h>

#include "zif-config.h"
#include "zif-package-rhn.h"
#include "zif-store-rhn.h"
#include "zif-utils.h"

#define ZIF_STORE_RHN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_RHN, ZifStoreRhnPrivate))

struct _ZifStoreRhnPrivate
{
	gchar			*channel;
	gchar			*server;
	gchar			*session_key;
	SoupSession		*session;
	ZifConfig		*config;
	ZifPackageRhnPrecache	 precache;
};

/* picked from thin air */
#define ZIF_STORE_RHN_MAX_THREADS	50

G_DEFINE_TYPE (ZifStoreRhn, zif_store_rhn, ZIF_TYPE_STORE)
static gpointer zif_store_rhn_object = NULL;

/**
 * zif_store_rhn_set_server:
 * @store: A #ZifStoreRhn
 * @server: The server to use, e.g. "https://rhn.redhat.com/rpc/api"
 *
 * Sets the XMLRPC server to use for RHN.
 *
 * Since: 0.1.6
 **/
void
zif_store_rhn_set_server (ZifStoreRhn *store,
			  const gchar *server)
{
	g_return_if_fail (ZIF_IS_STORE_RHN (store));
	g_return_if_fail (server != NULL);
	g_free (store->priv->server);
	store->priv->server = g_strdup (server);
}

/**
 * zif_store_rhn_set_channel:
 * @store: A #ZifStoreRhn
 * @channel: The server to use, e.g. "rhel-i386-client-6"
 *
 * Sets the RHN channel to use.
 *
 * Since: 0.1.6
 **/
void
zif_store_rhn_set_channel (ZifStoreRhn *store,
			   const gchar *channel)
{
	g_return_if_fail (ZIF_IS_STORE_RHN (store));
	g_return_if_fail (channel != NULL);
	g_free (store->priv->channel);
	store->priv->channel = g_strdup (channel);
}

/**
 * zif_store_rhn_set_precache:
 * @store: A #ZifStoreRhn
 * @precache: The data to cache, e.g. %ZIF_PACKAGE_RHN_PRECACHE_GET_DETAILS
 *
 * Sets the precache policy. Precaching slows down zif_store_load() but
 * dramatically speeds up any data access because each request is
 * multithreaded on up to 50 threads at once.
 *
 * Since: 0.1.6
 **/
void
zif_store_rhn_set_precache (ZifStoreRhn *store,
			    ZifPackageRhnPrecache precache)
{
	g_return_if_fail (ZIF_IS_STORE_RHN (store));
	store->priv->precache = precache;
}

/**
 * zif_store_rhn_login:
 * @store: A #ZifStoreRhn
 * @username: The username to login to RHN with
 * @password: The password to login to RHN with
 * @error: A #GError, or %NULL
 *
 * Logs into RHN using the specified username and password.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.6
 **/
gboolean
zif_store_rhn_login (ZifStoreRhn *store,
		     const gchar *username,
		     const gchar *password,
		     GError **error)
{
	gboolean ret = FALSE;
	gchar *session_key = NULL;
	GError *error_local = NULL;
	SoupMessage *msg = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_RHN (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no login */
	if (store->priv->server == NULL) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "xmlrpm server not set");
		goto out;
	}

	/* create request */
	msg = soup_xmlrpc_request_new (store->priv->server,
				       "auth.login",
				       G_TYPE_STRING, username,
				       G_TYPE_STRING, password,
				       G_TYPE_INVALID);

	/* send message */
	soup_session_send_message (store->priv->session, msg);
	ret = SOUP_STATUS_IS_SUCCESSFUL (msg->status_code);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "%s (error #%d)",
			     msg->reason_phrase,
			     msg->status_code);
		goto out;
	}

	/* get response */
	ret = soup_xmlrpc_extract_method_response (msg->response_body->data,
						   msg->response_body->length,
						   &error_local,
						   G_TYPE_STRING, &session_key);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "Could not parse XML-RPC response for %s (%i): %s",
			     msg->response_body->data,
			     (guint) msg->response_body->length,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* success */
	store->priv->session_key = g_strdup (session_key);
out:
	if (msg != NULL)
		g_object_unref (msg);
	g_free (session_key);
	return ret;
}

/**
 * zif_store_rhn_logout:
 * @store: A #ZifStoreRhn
 * @error: A #GError, or %NULL
 *
 * Logs out of RHN.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.6
 **/
gboolean
zif_store_rhn_logout (ZifStoreRhn *store,
		      GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gint retval;
	SoupMessage *msg = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_RHN (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no login */
	if (store->priv->session_key == NULL) {
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "not logged in");
		goto out;
	}

	/* create request */
	msg = soup_xmlrpc_request_new (store->priv->server,
				       "auth.logout",
				       G_TYPE_STRING, store->priv->session_key,
				       G_TYPE_INVALID);

	/* send message */
	soup_session_send_message (store->priv->session, msg);
	ret = SOUP_STATUS_IS_SUCCESSFUL (msg->status_code);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "%s (error #%d)",
			     msg->reason_phrase,
			     msg->status_code);
		goto out;
	}

	/* get response */
	ret = soup_xmlrpc_extract_method_response (msg->response_body->data,
						   msg->response_body->length,
						   &error_local,
						   G_TYPE_INT, &retval);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "Could not parse XML-RPC response for %s (%i): %s",
			     msg->response_body->data,
			     (guint) msg->response_body->length,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	//FIXME: what does this value mean? */
	g_debug ("logged off with status code %i", retval);
	g_free (store->priv->session_key);
	store->priv->session_key = NULL;
out:
	if (msg != NULL)
		g_object_unref (msg);
	return ret;
}

/**
 * zif_store_rhn_get_version:
 * @store: A #ZifStoreRhn
 * @error: A #GError, or %NULL
 *
 * Gets the RHN version.
 *
 * Return value: a string for success, %NULL otherwise
 *
 * Since: 0.1.6
 **/
gchar *
zif_store_rhn_get_version (ZifStoreRhn *store, GError **error)
{
	gboolean ret;
	gchar *version = NULL;
	GError *error_local = NULL;
	SoupMessage *msg;

	g_return_val_if_fail (ZIF_IS_STORE_RHN (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create request */
	msg = soup_xmlrpc_request_new (store->priv->server,
				       "api.getVersion",
				       G_TYPE_INVALID);

	/* send message */
	soup_session_send_message (store->priv->session, msg);
	ret = SOUP_STATUS_IS_SUCCESSFUL (msg->status_code);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "%s (error #%d)",
			     msg->reason_phrase,
			     msg->status_code);
		goto out;
	}

	/* get response */
	ret = soup_xmlrpc_extract_method_response (msg->response_body->data,
						   msg->response_body->length,
						   &error_local,
						   G_TYPE_STRING, &version);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "Could not parse XML-RPC response for %s (%i): %s",
			     msg->response_body->data,
			     (guint) msg->response_body->length,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_object_unref (msg);
	return version;
}

/**
 * zif_store_rhn_get_session_key:
 * @store: A #ZifStoreRhn
 *
 * Gets the session_key to use for the install root.
 *
 * Return value: The install session_key, e.g. "/"
 *
 * Since: 0.1.3
 **/
const gchar *
zif_store_rhn_get_session_key (ZifStoreRhn *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_RHN (store), NULL);
	return store->priv->session_key;
}

/**
 * zif_store_rhn_add_package:
 **/
static ZifPackage *
zif_store_rhn_add_package (ZifStore *store,
			   GHashTable *hash,
			   GError **error)
{
	gboolean ret;
	gchar *id;
	GValue *arch;
	GValue *epoch;
	GValue *name;
	GValue *release;
	GValue *rhn_id;
	GValue *version;
	ZifPackage *package = NULL;
	ZifPackage *package_tmp = NULL;
	ZifStoreRhn *rhn = ZIF_STORE_RHN (store);

	/* generate the id */
	name = g_hash_table_lookup (hash, "package_name");
	epoch = g_hash_table_lookup (hash, "package_epoch");
	version = g_hash_table_lookup (hash, "package_version");
	release = g_hash_table_lookup (hash, "package_release");
	arch = g_hash_table_lookup (hash, "package_arch_label");
	id = zif_package_id_from_nevra (g_value_get_string (name),
					atoi (g_value_get_string (epoch)),
					g_value_get_string (version),
					g_value_get_string (release),
					g_value_get_string (arch),
					"rhn");

	/* create the package */
	package_tmp = zif_package_rhn_new ();
	ret = zif_package_set_id (package_tmp, id, error);
	if (!ret)
		goto out;

	/* add RHN specific attributes */
	rhn_id = g_hash_table_lookup (hash, "package_id");
	zif_package_rhn_set_id (ZIF_PACKAGE_RHN (package_tmp),
				g_value_get_int (rhn_id));
	zif_package_rhn_set_session_key (ZIF_PACKAGE_RHN (package_tmp),
					 rhn->priv->session_key);
	zif_package_rhn_set_server (ZIF_PACKAGE_RHN (package_tmp),
				    rhn->priv->server);

	/* add it to the generic store */
	ret = zif_store_add_package (store, package_tmp, error);
	if (!ret)
		goto out;

	/* success */
	package = g_object_ref (package_tmp);
out:
	if (package_tmp != NULL)
		g_object_unref (package_tmp);
	g_free (id);
	return package;
}

/**
 * zif_store_rhn_coldplug_cb:
 **/
static void
zif_store_rhn_coldplug_cb (ZifPackage *package, ZifStoreRhn *rhn)
{
	gboolean ret;
	GError *error = NULL;
	GTimer *timer = g_timer_new ();

	/* coldplug */
	ret = zif_package_rhn_precache (ZIF_PACKAGE_RHN (package),
					rhn->priv->precache,
					&error);
	if (!ret) {
		g_warning ("failed to precache %s: %s",
			   zif_package_get_printable (package),
			   error->message);
		g_error_free (error);
		goto out;
	}
	g_debug ("coldplug of %s took %fms",
		 zif_package_get_printable (package),
		 g_timer_elapsed (timer, NULL) * 1000);
out:
	g_timer_destroy (timer);
}

/**
 * zif_store_rhn_load:
 **/
static gboolean
zif_store_rhn_load (ZifStore *store,
		    ZifState *state,
		    GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	GHashTable *hash;
	GThreadPool *pool = NULL;
	guint i;
	GValueArray *array = NULL;
	SoupMessage *msg = NULL;
	ZifPackage *package;
	ZifStoreRhn *rhn = ZIF_STORE_RHN (store);

	g_return_val_if_fail (ZIF_IS_STORE_RHN (store), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* set steps */
	ret = zif_state_set_steps (state,
				   error,
				   90, /* do xmlrpc request */
				   10, /* add packages */
				   -1);
	if (!ret)
		goto out;

	/* no login */
	if (rhn->priv->session_key == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
				     "no session key, not logged in");
		goto out;
	}

	/* no channel */
	if (rhn->priv->channel == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "no channel set");
		goto out;
	}

	/* get all the packages */
	msg = soup_xmlrpc_request_new (rhn->priv->server,
				       "channel.software.listLatestPackages",
				       G_TYPE_STRING, rhn->priv->session_key,
				       G_TYPE_STRING, rhn->priv->channel,
				       G_TYPE_INVALID);

	/* send message */
	soup_session_send_message (rhn->priv->session, msg);
	ret = SOUP_STATUS_IS_SUCCESSFUL (msg->status_code);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "%s (error #%d)",
			     msg->reason_phrase,
			     msg->status_code);
		goto out;
	}

	/* get response */
	ret = soup_xmlrpc_extract_method_response (msg->response_body->data,
						   msg->response_body->length,
						   &error_local,
						   G_TYPE_VALUE_ARRAY, &array);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "Could not parse XML-RPC response for %s (%i): %s",
			     msg->response_body->data,
			     (guint) msg->response_body->length,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* optionally coldplug all the RHN packages */
	pool = g_thread_pool_new ((GFunc) zif_store_rhn_coldplug_cb,
				  rhn, ZIF_STORE_RHN_MAX_THREADS, TRUE, NULL);

	/* get packages */
	g_debug ("got %i elements", array->n_values);
	for (i=0; i<array->n_values; i++) {
		hash = g_value_get_boxed (&array->values[i]);
		package = zif_store_rhn_add_package (store, hash, error);
		if (package == NULL) {
			ret = FALSE;
			goto out;
		}

		/* coldplug this */
		if (rhn->priv->precache > 0)
			g_thread_pool_push (pool, package, NULL);

		g_object_unref (package);
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (pool != NULL)
		g_thread_pool_free (pool, FALSE, TRUE);
	if (array != NULL)
		g_value_array_free (array);
	if (msg != NULL)
		g_object_unref (msg);
	return ret;
}

/**
 * zif_store_rhn_get_id:
 **/
static const gchar *
zif_store_rhn_get_id (ZifStore *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_RHN (store), NULL);
	return "rhn";
}

/**
 * zif_store_rhn_finalize:
 **/
static void
zif_store_rhn_finalize (GObject *object)
{
	ZifStoreRhn *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_RHN (object));
	store = ZIF_STORE_RHN (object);

	g_object_unref (store->priv->config);
	g_object_unref (store->priv->session);
	g_free (store->priv->session_key);
	g_free (store->priv->channel);
	g_free (store->priv->server);

	G_OBJECT_CLASS (zif_store_rhn_parent_class)->finalize (object);
}

/**
 * zif_store_rhn_class_init:
 **/
static void
zif_store_rhn_class_init (ZifStoreRhnClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifStoreClass *store_class = ZIF_STORE_CLASS (klass);
	object_class->finalize = zif_store_rhn_finalize;

	/* map */
	store_class->load = zif_store_rhn_load;
	store_class->get_id = zif_store_rhn_get_id;

	g_type_class_add_private (klass, sizeof (ZifStoreRhnPrivate));
}

/**
 * zif_store_rhn_init:
 **/
static void
zif_store_rhn_init (ZifStoreRhn *store)
{
	store->priv = ZIF_STORE_RHN_GET_PRIVATE (store);
	store->priv->config = zif_config_new ();
	store->priv->session = soup_session_sync_new ();
}

/**
 * zif_store_rhn_new:
 *
 * Return value: A new #ZifStoreRhn instance.
 *
 * Since: 0.1.6
 **/
ZifStore *
zif_store_rhn_new (void)
{
	if (zif_store_rhn_object != NULL) {
		g_object_ref (zif_store_rhn_object);
	} else {
		zif_store_rhn_object = g_object_new (ZIF_TYPE_STORE_RHN, NULL);
		g_object_add_weak_pointer (zif_store_rhn_object, &zif_store_rhn_object);
	}
	return ZIF_STORE (zif_store_rhn_object);
}

