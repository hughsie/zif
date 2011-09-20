/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-download
 * @short_description: Download packages
 *
 * This object is a simple wrapper around libsoup that handles
 * mirrorlists, timeouts and retries.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <libsoup/soup.h>

#include "zif-config.h"
#include "zif-download.h"
#include "zif-state.h"
#include "zif-md-metalink.h"
#include "zif-md-mirrorlist.h"

#define ZIF_DOWNLOAD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_DOWNLOAD, ZifDownloadPrivate))

typedef struct {
	gchar			*uri;
	guint			 retries;
} ZifDownloadItem;

/**
 * ZifDownloadPrivate:
 *
 * Private #ZifDownload data
 **/
struct _ZifDownloadPrivate
{
	GPtrArray		*array;
	SoupSession		*session;
	ZifConfig		*config;
};

typedef struct {
	gchar			*uri;
	GTimer			*timer;
	guint			 last_percentage;
	goffset			 last_body_length;
	SoupMessage		*msg;
	ZifDownload		*download;
	ZifState		*state;
} ZifDownloadFlight;

typedef enum {
	ZIF_DOWNLOAD_POLICY_LINEAR,
	ZIF_DOWNLOAD_POLICY_RANDOM,
	ZIF_DOWNLOAD_POLICY_LAST
} ZifDownloadPolicy;

G_DEFINE_TYPE (ZifDownload, zif_download, G_TYPE_OBJECT)

/**
 * zif_download_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_download_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_download_error");
	return quark;
}

/**
 * zif_download_item_free:
 **/
static void
zif_download_item_free (ZifDownloadItem *item)
{
	g_free (item->uri);
	g_free (item);
}

/**
 * zif_download_file_got_chunk_cb:
 **/
static void
zif_download_file_got_chunk_cb (SoupMessage *msg, SoupBuffer *chunk,
				ZifDownloadFlight *flight)
{
	guint percentage;
	goffset header_size;
	goffset body_length;
	gboolean ret;
	GCancellable *cancellable;
	guint64 speed;

	/* cancelled? */
	cancellable = zif_state_get_cancellable (flight->state);
	if (g_cancellable_is_cancelled (cancellable)) {
		g_debug ("cancelling download on %p", cancellable);
		soup_session_cancel_message (flight->download->priv->session,
					     msg,
					     SOUP_STATUS_CANCELLED);
		goto out;
	}

	/* if it's returning "Found" or an error, ignore the percentage */
	if (msg->status_code != SOUP_STATUS_OK) {
		g_debug ("ignoring status code %i (%s)",
			 msg->status_code, msg->reason_phrase);
		goto out;
	}

	/* get data */
	body_length = msg->response_body->length;
	header_size = soup_message_headers_get_content_length (msg->response_headers);

	/* size is not known */
	if (header_size < body_length)
		goto out;

	/* calulate percentage */
	percentage = (100 * body_length) / header_size;
	ret = zif_state_set_percentage (flight->state, percentage);
	if (ret) {
		/* only print if it's significant */
		g_debug ("download: %i%% (%" G_GOFFSET_FORMAT ", %" G_GOFFSET_FORMAT ")",
			 percentage, body_length, header_size);
	}

	/* only print whole percentage points */
	if (percentage != flight->last_percentage) {
		g_debug ("%s at %i%%", flight->uri, percentage);
		flight->last_percentage = percentage;

		/* work out speed */
		speed = (body_length - flight->last_body_length) /
				g_timer_elapsed (flight->timer, NULL);
		zif_state_set_speed (flight->state, speed);

		/* save for next time */
		flight->last_body_length = body_length;
		g_timer_reset (flight->timer);
	}
out:
	return;
}

/**
 * zif_download_file_finished_cb:
 **/
static void
zif_download_file_finished_cb (SoupMessage *msg, ZifDownloadFlight *flight)
{
	g_debug ("%s done!", flight->uri);
}

/**
 * zif_download_check_content_types:
 **/
static gboolean
zif_download_check_content_types (GFile *file,
				  const gchar *content_types_expected,
				  GError **error)
{
	const gchar *content_type;
	gboolean ret = FALSE;
	gchar **expected = NULL;
	GError *error_local = NULL;
	GFileInfo *info = NULL;
	guint i;

	/* no data */
	if (content_types_expected == NULL) {
		ret = TRUE;
		goto out;
	}

	/* get content type */
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  0,
				  NULL,
				  &error_local);
	if (info == NULL) {
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_WRONG_CONTENT_TYPE,
			     "failed to detect content type: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check it's what we expect */
	content_type = g_file_info_get_attribute_string (info,
							 G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
	expected = g_strsplit (content_types_expected, ",", -1);
	for (i=0; expected[i] != NULL; i++) {
		ret = g_strcmp0 (content_type, expected[i]) == 0;
		if (ret)
			break;
	}
	if (!ret) {
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_WRONG_CONTENT_TYPE,
			     "content type incorrect: got %s but expected %s",
			     content_type, content_types_expected);
		goto out;
	}
out:
	g_strfreev (expected);
	if (info != NULL)
		g_object_unref (info);
	return ret;
}

/**
 * zif_download_file_ftp:
 **/
static gboolean
zif_download_file_ftp (ZifDownload *download,
		       const gchar *uri,
		       const gchar *filename,
		       ZifState *state,
		       GError **error)
{
	gboolean ret;
	gchar *cmdline = NULL;
	gchar *password = NULL;
	gchar *username = NULL;
	gchar *standard_error = NULL;
	gint exit_status = 0;
	GPtrArray *array;
	guint retries;
	guint timeout;
	ZifDownloadPrivate *priv = download->priv;

	/* add arguments */
	array = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (array, g_strdup ("/usr/bin/wget"));
	retries = zif_config_get_uint (priv->config, "retries", NULL);
	if (retries > 0) {
		g_ptr_array_add (array, g_strdup_printf ("--tries=%i",
							 retries));
	}
	timeout = zif_config_get_uint (priv->config, "timeout", NULL);
	if (timeout > 0) {
		g_ptr_array_add (array, g_strdup_printf ("--timeout=%i",
							 timeout));
	}
	g_ptr_array_add (array, g_strdup_printf ("--output-document=%s",
						 filename));
	username = zif_config_get_string (priv->config, "proxy_username", NULL);
	if (username != NULL && username[0] != '\0') {
		g_ptr_array_add (array, g_strdup_printf ("--username=%s",
							 username));
		g_ptr_array_add (array, g_strdup_printf ("--ftp-user=%s",
							 username));
	}
	password = zif_config_get_string (priv->config, "proxy_password", NULL);
	if (password != NULL && password[0] != '\0') {
		g_ptr_array_add (array, g_strdup_printf ("--password=%s",
							 password));
		g_ptr_array_add (array, g_strdup_printf ("--ftp-password=%s",
							 password));
	}
	g_ptr_array_add (array, g_strdup (uri));
	g_ptr_array_add (array, NULL);

	/* try to spawn it and wait for success */
	cmdline = g_strjoinv (" ", (gchar **) array->pdata);
	g_debug ("running %s", cmdline);
	ret = g_spawn_sync (NULL, /* working_directory */
			    (gchar **) array->pdata,
			    NULL, /* envp */
			    /*G_SPAWN_STDOUT_TO_DEV_NULL*/ 0,
			    NULL, NULL,
			    NULL, /* standard_out */
			    &standard_error,
			    &exit_status,
			    error);
	if (!ret)
		goto out;

	/* wget failed */
	if (exit_status != 0) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_FAILED,
			     "failed to run wget: %s",
			     standard_error);
		goto out;
	}
out:
	g_free (cmdline);
	g_free (password);
	g_free (standard_error);
	g_free (username);
	g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_download_local_copy:
 **/
static gboolean
zif_download_local_copy (const gchar *uri, const gchar *filename, ZifState *state, GError **error)
{
	gboolean ret;
	GFile *source;
	GFile *dest;
	GCancellable *cancellable;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* just copy */
	source = g_file_new_for_path (uri);
	dest = g_file_new_for_path (filename);
	cancellable = zif_state_get_cancellable (state);
	ret = g_file_copy (source, dest, G_FILE_COPY_OVERWRITE, cancellable, NULL, NULL, error);
	if (!ret)
		goto out;
out:
	g_object_unref (source);
	g_object_unref (dest);
	return ret;
}

/**
 * zif_download_get_proxy:
 **/
static gchar *
zif_download_get_proxy (ZifDownload *download)
{
	gchar *http_proxy = NULL;
	gchar *password = NULL;
	gchar *proxy = NULL;
	gchar *username = NULL;
	GString *string;

	/* whole string given */
	http_proxy = zif_config_get_string (download->priv->config, "http_proxy", NULL);
	if (http_proxy != NULL)
		goto out;

	/* have we specified any proxy at all? */
	proxy = zif_config_get_string (download->priv->config, "proxy", NULL);
	if (proxy == NULL || proxy[0] == '\0')
		goto out;

	/* these are optional */
	username = zif_config_get_string (download->priv->config, "username", NULL);
	password = zif_config_get_string (download->priv->config, "password", NULL);

	/* join it all up */
	string = g_string_new ("http://");
	if (username != NULL && password != NULL)
		g_string_append_printf (string, "%s:%s@", username, password);
	else if (username != NULL && username[0] != '\0')
		g_string_append_printf (string, "%s@", username);
	else if (password != NULL && password[0] != '\0')
		g_string_append_printf (string, ":%s@", password);
	g_string_append (string, proxy);

	/* return bare char data */
	http_proxy = g_string_free (string, FALSE);
out:
	g_free (proxy);
	g_free (username);
	g_free (password);
	return http_proxy;
}

/**
 * zif_download_setup_session:
 **/
static gboolean
zif_download_setup_session (ZifDownload *download, GError **error)
{
	gboolean ret = FALSE;
	SoupURI *proxy = NULL;
	gchar *http_proxy = NULL;
	guint timeout;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get default value from the config file */
	timeout = zif_config_get_uint (download->priv->config,
				       "timeout", NULL);
	if (timeout == G_MAXUINT)
		timeout = 5;

	/* get the proxy from the config */
	http_proxy = zif_download_get_proxy (download);
	if (http_proxy != NULL) {
		g_debug ("using proxy %s", http_proxy);
		proxy = soup_uri_new (http_proxy);
	}

	/* setup the session */
	download->priv->session = soup_session_sync_new_with_options (SOUP_SESSION_PROXY_URI, proxy,
								      SOUP_SESSION_USER_AGENT, "zif",
								      SOUP_SESSION_TIMEOUT, timeout,
								      NULL);
	if (download->priv->session == NULL) {
		g_set_error_literal (error,
				     ZIF_DOWNLOAD_ERROR,
				     ZIF_DOWNLOAD_ERROR_FAILED,
				     "could not setup session");
		goto out;
	}
	ret = TRUE;
out:
	g_free (http_proxy);
	if (proxy != NULL)
		soup_uri_free (proxy);
	return ret;
}

/**
 * zif_download_file:
 * @download: A #ZifDownload
 * @uri: A full remote URI
 * @filename: A local filename to save to
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Downloads a file either from a remote site, or copying the file
 * from the local filesystem.
 *
 * This function will return with an error if the downloaded file
 * has zero size.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_download_file (ZifDownload *download,
		   const gchar *uri,
		   const gchar *filename,
		   ZifState *state,
		   GError **error)
{
	gboolean ret = FALSE;
	SoupURI *base_uri = NULL;
	GFile *file = NULL;
	GError *error_local = NULL;
	ZifDownloadFlight *flight = NULL;
	ZifDownloadError download_error = ZIF_DOWNLOAD_ERROR_FAILED;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* local file */
	if (g_str_has_prefix (uri, "/")) {
		ret = zif_download_local_copy (uri, filename, state, error);
		goto out;
	}

	/* FTP file */
	if (g_str_has_prefix (uri, "ftp://")) {
		ret = zif_download_file_ftp (download,
					     uri,
					     filename,
					     state,
					     error);
		goto out;
	}

	/* create session if it does not exist yet */
	if (download->priv->session == NULL) {
		ret = zif_download_setup_session (download, error);
		if (!ret)
			goto out;
	}

	/* save an instance of the state object */
	flight = g_new0 (ZifDownloadFlight, 1);
	flight->state = g_object_ref (state);
	flight->download = g_object_ref (download);
	flight->uri = g_path_get_basename (uri);
	flight->timer = g_timer_new ();

	base_uri = soup_uri_new (uri);
	if (base_uri == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_FAILED,
			     "could not parse uri: %s",
			     uri);
		goto out;
	}

	/* GET package */
	flight->msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (flight->msg == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_DOWNLOAD_ERROR,
				     ZIF_DOWNLOAD_ERROR_FAILED,
				     "could not setup message");
		goto out;
	}

	/* we want progress updates */
	g_signal_connect (flight->msg, "got-chunk",
			  G_CALLBACK (zif_download_file_got_chunk_cb),
			  flight);
	g_signal_connect (flight->msg, "finished",
			  G_CALLBACK (zif_download_file_finished_cb),
			  flight);

	/* set action */
	zif_state_action_start (state, ZIF_STATE_ACTION_DOWNLOADING, filename);

	/* send sync */
	soup_session_send_message (download->priv->session, flight->msg);

	/* find length */
	if (flight->msg->status_code == SOUP_STATUS_CANCELLED) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_STATE_ERROR,
				     ZIF_STATE_ERROR_CANCELLED,
				     soup_status_get_phrase (flight->msg->status_code));
		goto out;
	} else if (!SOUP_STATUS_IS_SUCCESSFUL (flight->msg->status_code)) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_WRONG_STATUS,
			     "failed to get valid response for %s: %s",
			     uri,
			     soup_status_get_phrase (flight->msg->status_code));
		goto out;
	}

	/* empty file */
	if (flight->msg->response_body->length == 0) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_DOWNLOAD_ERROR,
				     ZIF_DOWNLOAD_ERROR_WRONG_SIZE,
				     "remote file has zero size");
		goto out;
	}

	/* write file */
	file = g_file_new_for_path (filename);
	ret = g_file_replace_contents (file,
				       flight->msg->response_body->data,
				       flight->msg->response_body->length,
				       NULL, FALSE,
				       G_FILE_CREATE_NONE,
				       NULL, NULL, &error_local);
	if (!ret) {
		/* some errors are special */
		if (error_local->code == G_IO_ERROR_PERMISSION_DENIED)
			download_error = ZIF_DOWNLOAD_ERROR_PERMISSION_DENIED;
		else if (error_local->code == G_IO_ERROR_NO_SPACE)
			download_error = ZIF_DOWNLOAD_ERROR_NO_SPACE;
		g_set_error (error, ZIF_DOWNLOAD_ERROR, download_error,
			     "failed to write file: %s",  error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	if (flight != NULL) {
		g_timer_destroy (flight->timer);
		g_object_unref (flight->state);
		g_object_unref (flight->download);
		if (flight->msg != NULL)
			g_object_unref (flight->msg);
		g_free (flight->uri);
		g_free (flight);
	}

	if (base_uri != NULL)
		soup_uri_free (base_uri);
	if (file != NULL)
		g_object_unref (file);
	return ret;
}

/**
 * zif_download_set_proxy:
 * @download: A #ZifDownload
 * @http_proxy: HTTP proxy, e.g. "http://10.0.0.1:8080"
 * @error: A #GError, or %NULL
 *
 * Sets the proxy used for downloading files.
 *
 * Do not use this method any more. Instead use:
 * zif_config_set_string(config, "http_proxy", "value", error)
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_download_set_proxy (ZifDownload *download, const gchar *http_proxy, GError **error)
{
	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* deprecated */
	g_warning ("invalid use of zif_download_set_proxy() - "
		   "set the proxy using zif_config_set_string(config, \"http_proxy\", \"value\", error)");
	return zif_config_set_string (download->priv->config, "http_proxy", http_proxy, error);
}

/**
 * zif_download_location_array_get_index:
 **/
static guint
zif_download_location_array_get_index (GPtrArray *array, const gchar *uri)
{
	guint i;
	ZifDownloadItem *item;

	/* find the uri */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (g_strcmp0 (item->uri, uri) == 0)
			return i;
	}
	return G_MAXUINT;
}

/**
 * zif_download_location_add_uri:
 * @download: A #ZifDownload
 * @uri: Full mirror URI, e.g. "http://dave.com/pub/"
 * @error: A #GError, or %NULL
 *
 * Adds a URI to be used when using zif_download_location_full().
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_download_location_add_uri (ZifDownload *download, const gchar *uri, GError **error)
{
	ZifDownloadItem *item;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already added */
	if (zif_download_location_array_get_index (download->priv->array, uri) != G_MAXUINT)
		goto out;

	/* add to array */
	item = g_new0 (ZifDownloadItem, 1);
	item->uri = g_strdup (uri);
	g_ptr_array_add (download->priv->array, item);
out:
	return TRUE;
}

/**
 * zif_download_location_add_array:
 * @download: A #ZifDownload
 * @array: Array of URI strings to add
 * @error: A #GError, or %NULL
 *
 * Adds an array of URIs to be used when using zif_download_location_full().
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_download_location_add_array (ZifDownload *download, GPtrArray *array, GError **error)
{
	guint i;
	gboolean ret = TRUE;
	const gchar *uri;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (array != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* add each uri */
	for (i=0; i<array->len; i++) {
		uri = g_ptr_array_index (array, i);
		ret = zif_download_location_add_uri (download, uri, error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * zif_download_location_add_md:
 * @download: A #ZifDownload
 * @md: A #ZifMd
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Adds an metadata source to be used when using zif_download_location_full().
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_download_location_add_md (ZifDownload *download, ZifMd *md, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (md != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* metalink */
	if (zif_md_get_kind (md) == ZIF_MD_KIND_METALINK) {
		array = zif_md_metalink_get_uris (ZIF_MD_METALINK (md), 50, state, error);
		if (array == NULL)
			goto out;
		ret = zif_download_location_add_array (download, array, error);
		goto out;
	}

	/* mirrorlist */
	if (zif_md_get_kind (md) == ZIF_MD_KIND_MIRRORLIST) {
		array = zif_md_mirrorlist_get_uris (ZIF_MD_MIRRORLIST (md), state, error);
		if (array == NULL)
			goto out;
		ret = zif_download_location_add_array (download, array, error);
		goto out;
	}

	/* nothing good to use */
	g_set_error (error,
		     ZIF_DOWNLOAD_ERROR,
		     ZIF_DOWNLOAD_ERROR_FAILED,
		     "md type %s is invalid",
		     zif_md_kind_to_text (zif_md_get_kind (md)));
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_download_location_remove_uri:
 * @download: A #ZifDownload
 * @uri: URI to remove
 * @error: A #GError, or %NULL
 *
 * Removes a URI from the pool used to download files.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_download_location_remove_uri (ZifDownload *download, const gchar *uri, GError **error)
{
	guint index;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* does not exist */
	index = zif_download_location_array_get_index (download->priv->array, uri);
	if (index == G_MAXUINT) {
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_FAILED,
			     "The URI %s does not already exist", uri);
		return FALSE;
	}

	/* remove from array */
	g_ptr_array_remove_index (download->priv->array, index);
	return TRUE;
}

/**
 * zif_download_check_size:
 **/
static gboolean
zif_download_check_size (GFile *file,
			 guint64 size,
			 GCancellable *cancellable,
			 GError **error)
{
	gboolean ret = FALSE;
	gchar *filename = NULL;
	GFileInfo *info = NULL;
	guint64 size_tmp = G_MAXUINT64;

	/* no data */
	if (size == 0) {
		ret = TRUE;
		goto out;
	}

	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_SIZE,
				  0,
				  cancellable,
				  error);
	if (info == NULL) {
		ret = FALSE;
		goto out;
	}
	size_tmp = g_file_info_get_attribute_uint64 (info,
						     G_FILE_ATTRIBUTE_STANDARD_SIZE);
	ret = (size == size_tmp);
	if (!ret) {
		filename = g_file_get_path (file);
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_WRONG_SIZE,
			     "incorrect size for %s: got %" G_GUINT64_FORMAT
			     " but expected %" G_GUINT64_FORMAT,
			     filename, size_tmp, size);
		goto out;
	}
out:
	g_free (filename);
	if (info != NULL)
		g_object_unref (info);
	return ret;
}

/**
 * zif_download_check_checksum:
 **/
static gboolean
zif_download_check_checksum (GFile *file,
			     GChecksumType checksum_type,
			     const gchar *checksum,
			     GError **error)
{
	gboolean ret;
	gchar *checksum_tmp = NULL;
	gchar *data = NULL;
	gchar *filename = NULL;
	gsize len;

	/* no data */
	if (checksum == NULL) {
		ret = TRUE;
		goto out;
	}

	filename = g_file_get_path (file);
	ret = g_file_get_contents (filename, &data, &len, error);
	if (!ret)
		goto out;
	checksum_tmp = g_compute_checksum_for_string (checksum_type, data, len);
	ret = (g_strcmp0 (checksum_tmp, checksum) == 0);
	if (!ret) {
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_WRONG_CHECKSUM,
			     "incorrect checksum for %s: got %s but expected %s",
			     filename, checksum_tmp, checksum);
		goto out;
	}
out:
	g_free (checksum_tmp);
	g_free (data);
	g_free (filename);
	return ret;
}

#if !GLIB_CHECK_VERSION(2,28,0)
#include <sys/time.h>
static gint64
_g_get_real_time (void)
{
	struct timeval tv;
	gettimeofday (&tv, NULL);
	return (tv.tv_sec * G_USEC_PER_SEC) + tv.tv_usec;
}
#endif

/**
 * zif_download_location_full:
 * @download: A #ZifDownload
 * @uri: A full remote URI.
 * @filename: Local filename to save to
 * @size: Expected size in bytes, or 0
 * @content_types: Comma delimited expected content types of the file, or %NULL
 * @checksum_type: Checksum type, e.g. %G_CHECKSUM_SHA256, or 0
 * @checksum: Expected checksum of the file, or %NULL
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Downloads a file either from a remote site, or copying the file
 * from the local filesystem, and then verifying it against what we are
 * expecting.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.2.1
 **/
gboolean
zif_download_file_full (ZifDownload *download,
			const gchar *uri,
			const gchar *filename,
			guint64 size,
			const gchar *content_types,
			GChecksumType checksum_type,
			const gchar *checksum,
			ZifState *state,
			GError **error)
{
	gboolean ret;
	GFile *file;
	GCancellable *cancellable;

	/* does file already exist and valid? */
	file = g_file_new_for_path (filename);
	cancellable = zif_state_get_cancellable (state);
	ret = g_file_query_exists (file, cancellable);
	if (ret &&
	    zif_download_check_size (file, size, cancellable, NULL) &&
	    zif_download_check_content_types (file, content_types, NULL) &&
	    zif_download_check_checksum (file, checksum_type, checksum, NULL)) {
		g_debug ("%s exists and is valid, skipping download",
			 filename);

		/* set the file mtime */
		ret = g_file_set_attribute_uint64 (file,
						   G_FILE_ATTRIBUTE_TIME_MODIFIED,
#if GLIB_CHECK_VERSION(2,28,0)
						   g_get_real_time () / G_USEC_PER_SEC,
#else
						   _g_get_real_time () / G_USEC_PER_SEC,
#endif
						   G_FILE_QUERY_INFO_NONE,
						   cancellable,
						   error);
		goto out;
	}

	/* download */
	ret = zif_download_file (download,
				 uri,
				 filename,
				 state,
				 error);
	if (!ret)
		goto out;

	/* verify size */
	ret = zif_download_check_size (file,
				       size,
				       cancellable,
				       error);
	if (!ret)
		goto out;

	/* check content type is what we expect */
	ret = zif_download_check_content_types (file,
						content_types,
						error);
	if (!ret)
		goto out;

	/* verify checksum */
	ret = zif_download_check_checksum (file,
					   checksum_type,
					   checksum,
					   error);
	if (!ret)
		goto out;
out:
	g_object_unref (file);
	return ret;
}

/**
 * _g_propagate_error_replace:
 **/
static void
_g_propagate_error_replace (GError **dest, GError *source)
{
	if (*dest != NULL)
		g_clear_error (dest);
	g_propagate_error (dest, source);
}

/**
 * zif_download_location_full:
 * @download: A #ZifDownload
 * @location: Location to add on to the end of the pool URIs
 * @filename: Local filename to save to
 * @size: Expected size in bytes, or 0
 * @content_types: Comma delimited expected content types of the file, or %NULL
 * @checksum_type: Checksum type, e.g. %G_CHECKSUM_SHA256, or 0
 * @checksum: Expected checksum of the file, or %NULL
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Downloads a file using a pool of download servers, and then verifying
 * it against what we are expecting.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_download_location_full (ZifDownload *download,
			    const gchar *location,
			    const gchar *filename,
			    guint64 size,
			    const gchar *content_types,
			    GChecksumType checksum_type,
			    const gchar *checksum,
			    ZifState *state,
			    GError **error)
{
	gboolean ret = FALSE;
	gboolean set_error = FALSE;
	gchar *failovermethod = NULL;
	gchar *uri_tmp;
	GError *error_local = NULL;
	GError *error_last = NULL;
	GPtrArray *array = NULL;
	guint index;
	guint retries;
	ZifDownloadItem *item;
	ZifDownloadPolicy policy = ZIF_DOWNLOAD_POLICY_RANDOM;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (location != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* nothing in the pool */
	array = download->priv->array;
	if (array->len == 0) {
		g_set_error_literal (error,
				     ZIF_DOWNLOAD_ERROR,
				     ZIF_DOWNLOAD_ERROR_NO_LOCATIONS,
				     "The download pool is empty");
		goto out;
	}

	/* get download policy */
	failovermethod = zif_config_get_string (download->priv->config,
						"failovermethod",
						NULL);
	if (g_strcmp0 (failovermethod, "ordered") == 0)
		policy = ZIF_DOWNLOAD_POLICY_LINEAR;

	/* keep trying until we get success */
	while (array->len > 0) {

		/* get the next mirror according to policy */
		if (policy == ZIF_DOWNLOAD_POLICY_RANDOM) {
			if (array->len > 1)
				index = g_random_int_range (0, array->len - 1);
			else
				index = 0;
		} else {
			index = 0;
		}

		/* form the full URL */
		item = g_ptr_array_index (array, index);
		uri_tmp = g_build_filename (item->uri, location, NULL);

		g_debug ("attempt to download %s", uri_tmp);
		zif_state_reset (state);
		ret = zif_download_file_full (download, uri_tmp, filename,
					      size, content_types, checksum_type, checksum,
					      state, &error_local);
		if (!ret) {
			/* some errors really are fatal */
			if (error_local->domain == ZIF_DOWNLOAD_ERROR &&
			    error_local->code == ZIF_DOWNLOAD_ERROR_PERMISSION_DENIED) {
				g_propagate_error (error, error_local);
				set_error = TRUE;
				break;
			}
			if (error_local->domain == ZIF_DOWNLOAD_ERROR &&
			    error_local->code == ZIF_DOWNLOAD_ERROR_NO_SPACE) {
				g_propagate_error (error, error_local);
				set_error = TRUE;
				break;
			}
			if (error_local->domain == ZIF_STATE_ERROR &&
			    error_local->code == ZIF_STATE_ERROR_CANCELLED) {
				g_propagate_error (error, error_local);
				set_error = TRUE;
				break;
			}

			/* increment the download count */
			item->retries++;

			/* too many retries */
			retries = zif_config_get_uint (download->priv->config,
						       "retries", error);
			if (retries == G_MAXUINT) {
				ret = FALSE;
				goto out;
			}
			if (item->retries >= retries) {

				/* just print and remove, not fatal */
				g_debug ("failed to download %s after try %i: %s, so removing",
					 item->uri,
					 item->retries,
					 error_local->message);

				/* save this for a better global error */
				_g_propagate_error_replace (&error_last, error_local);
				error_local = NULL;

				/* remove it (error is NULL as we know it exists, we just used it) */
				zif_download_location_remove_uri (download,
								  item->uri,
								  NULL);
			} else {
				/* just print, not fatal */
				g_debug ("failed to download %s: %s, on retry %i/%i",
					 item->uri,
					 error_local->message,
					 item->retries,
					 retries);

				/* save this for a better global error */
				_g_propagate_error_replace (&error_last, error_local);
				error_local = NULL;
			}
		} else {
			g_debug ("downloaded correct content %s into %s",
				 uri_tmp, filename);
		}

		g_free (uri_tmp);
		if (ret)
			break;
	}
	if (!ret && !set_error) {
		if (error_last != NULL) {
			g_set_error (error,
				     error_last->domain,
				     error_last->code,
				     "Failed to download: %s",
				     error_last->message);
		} else {
			g_set_error (error,
				     ZIF_DOWNLOAD_ERROR,
				     ZIF_DOWNLOAD_ERROR_NO_LOCATIONS,
				     "Failed to download %s from any mirrors",
				     location);
		}
	}
out:
	if (error_last != NULL)
		g_error_free (error_last);
	g_free (failovermethod);
	return ret;
}

/**
 * zif_download_location:
 * @download: A #ZifDownload
 * @location: Location to add on to the end of the pool URIs
 * @filename: Local filename to save to
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Downloads a file using a pool of download servers.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_download_location (ZifDownload *download, const gchar *location, const gchar *filename,
		       ZifState *state, GError **error)
{
	return zif_download_location_full (download, location, filename, 0, NULL, 0, NULL, state, error);
}

/**
 * zif_download_location_get_size:
 * @download: A #ZifDownload
 *
 * Gets the number of active mirrors we can use.
 *
 * Return value: A number or active URIs
 *
 * Since: 0.1.3
 **/
guint
zif_download_location_get_size (ZifDownload *download)
{
	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), 0);
	return download->priv->array->len;
}

/**
 * zif_download_location_clear:
 * @download: A #ZifDownload
 *
 * Clears the list of active mirrors.
 *
 * Since: 0.1.3
 **/
void
zif_download_location_clear (ZifDownload *download)
{
	g_return_if_fail (ZIF_IS_DOWNLOAD (download));
	g_ptr_array_set_size (download->priv->array, 0);
}

/**
 * zif_download_finalize:
 **/
static void
zif_download_finalize (GObject *object)
{
	ZifDownload *download;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_DOWNLOAD (object));
	download = ZIF_DOWNLOAD (object);

	if (download->priv->session != NULL)
		g_object_unref (download->priv->session);
	g_object_unref (download->priv->config);
	g_ptr_array_unref (download->priv->array);

	G_OBJECT_CLASS (zif_download_parent_class)->finalize (object);
}

/**
 * zif_download_class_init:
 **/
static void
zif_download_class_init (ZifDownloadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_download_finalize;

	g_type_class_add_private (klass, sizeof (ZifDownloadPrivate));
}

/**
 * zif_download_init:
 **/
static void
zif_download_init (ZifDownload *download)
{
	download->priv = ZIF_DOWNLOAD_GET_PRIVATE (download);
	download->priv->session = NULL;
	download->priv->config = zif_config_new ();
	download->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_download_item_free);
}

/**
 * zif_download_new:
 *
 * Return value: A new download instance.
 *
 * Since: 0.1.0
 **/
ZifDownload *
zif_download_new (void)
{
	ZifDownload *download;
	download = g_object_new (ZIF_TYPE_DOWNLOAD, NULL);
	return ZIF_DOWNLOAD (download);
}

