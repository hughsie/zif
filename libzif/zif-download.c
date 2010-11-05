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
 * @short_description: Generic object to download packages.
 *
 * This object is a trivial wrapper around libsoup.
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

/**
 * ZifDownloadPrivate:
 *
 * Private #ZifDownload data
 **/
struct _ZifDownloadPrivate
{
	gchar			*proxy;
	SoupSession		*session;
	SoupMessage		*msg;
	ZifState		*state;
	ZifConfig		*config;
	GPtrArray		*array;
	ZifDownloadPolicy	 policy;
};

static gpointer zif_download_object = NULL;

G_DEFINE_TYPE (ZifDownload, zif_download, G_TYPE_OBJECT)

/**
 * zif_download_error_quark:
 *
 * Return value: Our personal error quark.
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
 * zif_download_file_got_chunk_cb:
 **/
static void
zif_download_file_got_chunk_cb (SoupMessage *msg, SoupBuffer *chunk, ZifDownload *download)
{
	guint percentage;
	guint header_size;
	guint body_length;
	gboolean ret;

	/* get data */
	body_length = (guint) msg->response_body->length;
	header_size = soup_message_headers_get_content_length (msg->response_headers);

	/* size is not known */
	if (header_size < body_length)
		goto out;

	/* calulate percentage */
	percentage = (100 * body_length) / header_size;
	ret = zif_state_set_percentage (download->priv->state, percentage);
	if (ret)
		g_debug ("download: %i%% (%i, %i) - %p, %p", percentage, body_length, header_size, msg, download);
out:
	return;
}

/**
 * zif_download_file_finished_cb:
 **/
static void
zif_download_file_finished_cb (SoupMessage *msg, ZifDownload *download)
{
	g_debug ("done!");
	g_object_unref (download->priv->msg);
	download->priv->msg = NULL;
}

/**
 * zif_download_cancelled_cb:
 **/
static void
zif_download_cancelled_cb (GCancellable *cancellable, ZifDownload *download)
{
	g_return_if_fail (ZIF_IS_DOWNLOAD (download));

	/* check we have a download */
	if (download->priv->msg == NULL) {
		g_debug ("nothing to cancel");
		return;
	}

	/* cancel */
	g_debug ("cancelling download");
	soup_session_cancel_message (download->priv->session, download->priv->msg, SOUP_STATUS_CANCELLED);
}

/**
 * zif_download_check_content_type:
 **/
static gboolean
zif_download_check_content_type (GFile *file, const gchar *content_type_expected, GError **error)
{
	GFileInfo *info;
	gboolean ret = FALSE;
	const gchar *content_type;
	GError *error_local = NULL;

	/* get content type */
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, 0, NULL, &error_local);
	if (info == NULL) {
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_FAILED,
			     "failed to detect content type: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check it's what we expect */
	content_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
	ret = g_strcmp0 (content_type, content_type_expected) == 0;
	if (!ret) {
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_FAILED,
			     "content type incorrect: got %s but expected %s",
			     content_type, content_type_expected);
		goto out;
	}
out:
	if (info != NULL)
		g_object_unref (info);
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
 * zif_download_file:
 * @download: the #ZifDownload object
 * @uri: the full remote URI
 * @filename: the local filename to save to
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Downloads a file either from a remote site, or copying the file
 * from the local filesystem.
 *
 * This function will return with an error if the downloaded file
 * has zero size.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_download_file (ZifDownload *download, const gchar *uri, const gchar *filename, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	SoupURI *base_uri = NULL;
	SoupMessage *msg = NULL;
	GError *error_local = NULL;
	gulong cancellable_id = 0;
	GCancellable *cancellable;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (download->priv->msg == NULL, FALSE);
	g_return_val_if_fail (download->priv->session != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* local file */
	if (g_str_has_prefix (uri, "/")) {
		ret = zif_download_local_copy (uri, filename, state, error);
		goto out;
	}

	/* save an instance of the state object */
	download->priv->state = g_object_ref (state);

	/* set up cancel */
	cancellable = zif_state_get_cancellable (state);
	if (cancellable != NULL) {
		g_cancellable_reset (cancellable);
		cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (zif_download_cancelled_cb), download, NULL);
	}

	base_uri = soup_uri_new (uri);
	if (base_uri == NULL) {
		g_set_error (error, ZIF_DOWNLOAD_ERROR, ZIF_DOWNLOAD_ERROR_FAILED,
			     "could not parse uri: %s", uri);
		goto out;
	}

	/* GET package */
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		g_set_error_literal (error, ZIF_DOWNLOAD_ERROR, ZIF_DOWNLOAD_ERROR_FAILED,
				     "could not setup message");
		goto out;
	}

	/* we want progress updates */
	g_signal_connect (msg, "got-chunk", G_CALLBACK (zif_download_file_got_chunk_cb), download);
	g_signal_connect (msg, "finished", G_CALLBACK (zif_download_file_finished_cb), download);

	/* we need this for cancelling */
	download->priv->msg = g_object_ref (msg);

	/* request */
//	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

	/* set action */
	zif_state_action_start (state, ZIF_STATE_ACTION_DOWNLOADING, filename);

	/* send sync */
	soup_session_send_message (download->priv->session, msg);

	/* find length */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		g_set_error (error, ZIF_DOWNLOAD_ERROR, ZIF_DOWNLOAD_ERROR_FAILED,
			     "failed to get valid response for %s: %s", uri, soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* empty file */
	if (msg->response_body->length == 0) {
		g_set_error_literal (error, ZIF_DOWNLOAD_ERROR, ZIF_DOWNLOAD_ERROR_FAILED,
				     "remote file has zero size");
		goto out;
	}

	/* write file */
	ret = g_file_set_contents (filename, msg->response_body->data, msg->response_body->length, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_DOWNLOAD_ERROR, ZIF_DOWNLOAD_ERROR_FAILED,
			     "failed to write file: %s",  error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	if (cancellable_id != 0)
		g_cancellable_disconnect (cancellable, cancellable_id);
	if (download->priv->state != NULL)
		g_object_unref (download->priv->state);
	download->priv->state = NULL;
	if (base_uri != NULL)
		soup_uri_free (base_uri);
	if (msg != NULL)
		g_object_unref (msg);
	return ret;
}

/**
 * zif_download_set_proxy:
 * @download: the #ZifDownload object
 * @http_proxy: the HTTP proxy, e.g. "http://10.0.0.1:8080"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets the proxy used for downloading files.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_download_set_proxy (ZifDownload *download, const gchar *http_proxy, GError **error)
{
	gboolean ret = FALSE;
	SoupURI *proxy = NULL;
	guint connection_timeout;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get default value from the config file */
	connection_timeout = zif_config_get_uint (download->priv->config, "connection_timeout", NULL);
	if (connection_timeout == G_MAXUINT)
		connection_timeout = 5;

	/* setup the session */
	if (http_proxy != NULL) {
		g_debug ("using proxy %s", http_proxy);
		proxy = soup_uri_new (http_proxy);
	}
	download->priv->session = soup_session_sync_new_with_options (SOUP_SESSION_PROXY_URI, proxy,
								      SOUP_SESSION_USER_AGENT, "zif",
								      SOUP_SESSION_TIMEOUT, connection_timeout,
								      NULL);
	if (download->priv->session == NULL) {
		g_set_error_literal (error, ZIF_DOWNLOAD_ERROR, ZIF_DOWNLOAD_ERROR_FAILED,
				     "could not setup session");
		goto out;
	}
	ret = TRUE;
out:
	if (proxy != NULL)
		soup_uri_free (proxy);
	return ret;
}

/**
 * zif_download_location_array_get_index:
 **/
static guint
zif_download_location_array_get_index (GPtrArray *array, const gchar *uri)
{
	guint i;
	const gchar *uri_tmp;

	/* find the uri */
	for (i=0; i<array->len; i++) {
		uri_tmp = g_ptr_array_index (array, i);
		if (g_strcmp0 (uri_tmp, uri) == 0)
			return i;
	}
	return G_MAXUINT;
}

/**
 * zif_download_location_add_uri:
 * @download: the #ZifDownload object
 * @uri: the full mirror URI, e.g. http://dave.com/pub/
 * @error: a #GError which is used on failure, or %NULL
 *
 * Adds a URI to be used when using zif_download_location_full().
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.3
 **/
gboolean
zif_download_location_add_uri (ZifDownload *download, const gchar *uri, GError **error)
{
	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already added */
	if (zif_download_location_array_get_index (download->priv->array, uri) != G_MAXUINT) {
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_FAILED,
			     "The URI %s as already been added", uri);
		return FALSE;
	}

	/* add to array */
	g_ptr_array_add (download->priv->array, g_strdup (uri));
	return TRUE;
}

/**
 * zif_download_location_add_array:
 * @download: the #ZifDownload object
 * @array: an array of URI string to add
 * @error: a #GError which is used on failure, or %NULL
 *
 * Adds an array of URIs to be used when using zif_download_location_full().
 *
 * Return value: %TRUE for success, %FALSE for failure
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
 * @download: the #ZifDownload object
 * @md: A valid #ZifMd object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Adds an metadata source to be used when using zif_download_location_full().
 *
 * Return value: %TRUE for success, %FALSE for failure
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
		     "md type %s is invalid", zif_md_kind_to_text (zif_md_get_kind (md)));
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_download_location_remove_uri:
 * @download: the #ZifDownload object
 * @uri: The full URI to remove
 * @error: a #GError which is used on failure, or %NULL
 *
 * Removes a URI from the pool used to download files.
 *
 * Return value: %TRUE for success, %FALSE for failure
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
 * zif_download_location_full_try:
 **/
static gboolean
zif_download_location_full_try (ZifDownload *download, const gchar *uri, const gchar *filename,
				guint64 size, const gchar *content_type,
				GChecksumType checksum_type, const gchar *checksum,
				ZifState *state, GError **error)
{
	gboolean ret;
	gchar *checksum_tmp = NULL;
	gchar *data = NULL;
	GFile *file;
	GFileInfo *info_size = NULL;
	gsize len;
	guint64 size_tmp;

	/* download */
	file = g_file_new_for_path (filename);
	ret = zif_download_file (download, uri, filename, state, error);
	if (!ret)
		goto out;

	/* verify size */
	if (size > 0) {
		info_size = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SIZE, 0, NULL, error);
		if (info_size == NULL) {
			ret = FALSE;
			goto out;
		}
		size_tmp = g_file_info_get_attribute_uint64 (info_size, G_FILE_ATTRIBUTE_STANDARD_SIZE);
		ret = (size_tmp == size);
		if (!ret) {
			g_set_error (error,
				     ZIF_DOWNLOAD_ERROR,
				     ZIF_DOWNLOAD_ERROR_FAILED,
				     "incorrect size for %s: got %" G_GUINT64_FORMAT
				     " but expected %" G_GUINT64_FORMAT,
				     filename, size_tmp, size);
			goto out;
		}
	}

	/* check content type is what we expect */
	if (content_type != NULL) {
		ret = zif_download_check_content_type (file, content_type, error);
		if (!ret)
			goto out;
	}

	/* verify checksum */
	if (checksum != NULL) {
		ret = g_file_get_contents (filename, &data, &len, error);
		if (!ret)
			goto out;
		checksum_tmp = g_compute_checksum_for_string (checksum_type, data, len);
		ret = (g_strcmp0 (checksum_tmp, checksum) == 0);
		if (!ret) {
			g_set_error (error,
				     ZIF_DOWNLOAD_ERROR,
				     ZIF_DOWNLOAD_ERROR_FAILED,
				     "incorrect checksum for %s: got %s but expected %s",
				     filename, checksum_tmp, checksum);
			goto out;
		}
	}
out:
	g_free (checksum_tmp);
	g_free (data);
	if (info_size != NULL)
		g_object_unref (info_size);
	g_object_unref (file);
	return ret;
}

/**
 * zif_download_location_full:
 * @download: the #ZifDownload object
 * @location: the location to add on to the end of the pool URIs
 * @filename: the local filename to save to
 * @size: the expected size in bytes, or 0
 * @content_type: the expected content type of the file, or %NULL
 * @checksum_type: the checksum type, e.g. %G_CHECKSUM_SHA256, or 0
 * @checksum: the expected checksum of the file, or %NULL
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Downloads a file using a pool of download servers, and then verifying
 * it against what we are expecting.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.3
 **/
gboolean
zif_download_location_full (ZifDownload *download, const gchar *location, const gchar *filename,
			    guint64 size, const gchar *content_type, GChecksumType checksum_type, const gchar *checksum,
			    ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	const gchar *uri;
	gchar *uri_tmp;
	guint index;
	GPtrArray *array = NULL;
	GError *error_local = NULL;

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
				     ZIF_DOWNLOAD_ERROR_FAILED,
				     "The download pool is empty");
		goto out;
	}

	/* keep trying until we get success */
	while (array->len > 0) {

		/* get the next mirror according to policy */
		if (download->priv->policy == ZIF_DOWNLOAD_POLICY_RANDOM) {
			if (array->len > 1)
				index = g_random_int_range (0, array->len - 1);
			else
				index = 0;
		} else {
			index = 0;
		}

		/* form the full URL */
		uri = g_ptr_array_index (array, index);
		uri_tmp = g_build_filename (uri, location, NULL);

		g_debug ("attempt to download %s", uri_tmp);
		zif_state_reset (state);
		ret = zif_download_location_full_try (download, uri_tmp, filename,
						  size, content_type, checksum_type, checksum,
						  state, &error_local);
		if (!ret) {
			g_debug ("failed to download %s: %s, so removing", uri, error_local->message);
			g_clear_error (&error_local);
			/* remove it (error is NULL as we know it exists, we just used it) */
			zif_download_location_remove_uri (download, uri, NULL);
		} else {
			g_debug ("downloaded correct content %s into %s", uri_tmp, filename);
		}

		g_free (uri_tmp);
		if (ret)
			break;
	}
	if (!ret) {
		g_set_error (error,
			     ZIF_DOWNLOAD_ERROR,
			     ZIF_DOWNLOAD_ERROR_FAILED,
			     "Failed to download %s from any mirrors", location);
		goto out;
	}
out:
	return ret;
}

/**
 * zif_download_location:
 * @download: the #ZifDownload object
 * @location: the location to add on to the end of the pool URIs
 * @filename: the local filename to save to
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Downloads a file using a pool of download servers.
 *
 * Return value: %TRUE for success, %FALSE for failure
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
 * zif_download_location_set_policy:
 * @download: the #ZifDownload object
 * @policy: the policy for choosing a URL, e.g. %ZIF_DOWNLOAD_POLICY_RANDOM
 *
 * Sets the policy for determining the next mirror to try.
 *
 * Since: 0.1.3
 **/
void
zif_download_location_set_policy (ZifDownload *download, ZifDownloadPolicy policy)
{
	g_return_if_fail (ZIF_IS_DOWNLOAD (download));
	download->priv->policy = policy;
}

/**
 * zif_download_location_get_size:
 * @download: the #ZifDownload object
 *
 * Gets the number of active mirrors we can use.
 *
 * Return value: the number or active URIs
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
 * zif_download_finalize:
 **/
static void
zif_download_finalize (GObject *object)
{
	ZifDownload *download;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_DOWNLOAD (object));
	download = ZIF_DOWNLOAD (object);

	g_free (download->priv->proxy);
	if (download->priv->msg != NULL)
		g_object_unref (download->priv->msg);
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
	download->priv->msg = NULL;
	download->priv->session = NULL;
	download->priv->proxy = NULL;
	download->priv->state = NULL;
	download->priv->config = zif_config_new ();
	download->priv->array = g_ptr_array_new_with_free_func (g_free);
	download->priv->policy = ZIF_DOWNLOAD_POLICY_RANDOM;
}

/**
 * zif_download_new:
 *
 * Return value: A new download class instance.
 *
 * Since: 0.1.0
 **/
ZifDownload *
zif_download_new (void)
{
	if (zif_download_object != NULL) {
		g_object_ref (zif_download_object);
	} else {
		zif_download_object = g_object_new (ZIF_TYPE_DOWNLOAD, NULL);
		g_object_add_weak_pointer (zif_download_object, &zif_download_object);
	}
	return ZIF_DOWNLOAD (zif_download_object);
}

