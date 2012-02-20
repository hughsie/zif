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
 * SECTION:zif-package-rhn
 * @short_description: RHN package
 *
 * This object is a subclass of #ZifPackage
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <libsoup/soup.h>
#include <stdlib.h>

#include "zif-package-private.h"
#include "zif-package-rhn.h"

#define ZIF_PACKAGE_RHN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_PACKAGE_RHN, ZifPackageRhnPrivate))

/**
 * ZifPackageRhnPrivate:
 *
 * Private #ZifPackageRhn data
 **/
struct _ZifPackageRhnPrivate
{
	gchar			*server;
	gchar			*session_key;
	guint			 id;
	SoupSession		*session;
};

G_DEFINE_TYPE (ZifPackageRhn, zif_package_rhn, ZIF_TYPE_PACKAGE)

/*
 * zif_package_rhn_get_details:
 */
static gboolean
zif_package_rhn_get_details (ZifPackageRhn *rhn,
			     ZifState *state,
			     GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GHashTable *hash = NULL;
	GValue *value;
	SoupMessage *msg;
	ZifPackage *pkg = ZIF_PACKAGE (rhn);
	ZifString *tmp;

	/* create request */
	msg = soup_xmlrpc_request_new (rhn->priv->server,
				       "packages.getDetails",
				       G_TYPE_STRING, rhn->priv->session_key,
				       G_TYPE_INT, rhn->priv->id,
				       G_TYPE_INVALID);

	/* send message */
	soup_session_send_message (rhn->priv->session, msg);
	ret = SOUP_STATUS_IS_SUCCESSFUL (msg->status_code);
	if (!ret) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "%s (error #%d)",
			     msg->reason_phrase,
			     msg->status_code);
		goto out;
	}

	/* get response */
	ret = soup_xmlrpc_extract_method_response (msg->response_body->data,
						   msg->response_body->length,
						   &error_local,
						   G_TYPE_HASH_TABLE, &hash);
	if (!ret) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "Could not parse XML-RPC response for %s (%i): %s",
			     msg->response_body->data,
			     (guint) msg->response_body->length,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* set summary */
	value = g_hash_table_lookup (hash, "package_summary");
	tmp = zif_string_new (g_value_get_string (value));
	zif_package_set_summary (pkg, tmp);
	zif_string_unref (tmp);

	/* set filename */
	value = g_hash_table_lookup (hash, "package_file");
	zif_package_set_cache_filename (pkg,
					g_value_get_string (value));

	/* set licence */
	value = g_hash_table_lookup (hash, "package_license");
	tmp = zif_string_new (g_value_get_string (value));
	zif_package_set_license (pkg, tmp);
	zif_string_unref (tmp);

	/* set description */
	value = g_hash_table_lookup (hash, "package_description");
	tmp = zif_string_new (g_value_get_string (value));
	zif_package_set_description (pkg, tmp);
	zif_string_unref (tmp);

	/* set checksum */
	value = g_hash_table_lookup (hash, "package_md5sum");
	tmp = zif_string_new (g_value_get_string (value));
//	zif_package_set_???? (pkg, tmp);
	zif_string_unref (tmp);

	/* set size */
	value = g_hash_table_lookup (hash, "package_size");
	zif_package_set_size (pkg, atoi (g_value_get_string (value)));

	/* we don't get group from RHN */
	tmp = zif_string_new ("unknown");
	zif_package_set_group (pkg, tmp);
	zif_string_unref (tmp);

	/* we don't get category from RHN */
	tmp = zif_string_new ("unknown");
	zif_package_set_category (pkg, tmp);
	zif_string_unref (tmp);

	/* we don't get homepage URL from RHN */
	tmp = zif_string_new ("https://rhn.redhat.com/");
	zif_package_set_url (pkg, tmp);
	zif_string_unref (tmp);

out:
	if (hash != NULL)
		g_hash_table_destroy (hash);
	g_object_unref (msg);
	return ret;
}

/*
 * zif_package_rhn_list_files:
 */
static gboolean
zif_package_rhn_list_files (ZifPackageRhn *rhn,
			    ZifState *state,
			    GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GHashTable *hash;
	GPtrArray *files = NULL;
	guint i;
	GArray *array = NULL;
	GValue *value;
	SoupMessage *msg;
	ZifPackage *pkg = ZIF_PACKAGE (rhn);

	/* create request */
	msg = soup_xmlrpc_request_new (rhn->priv->server,
				       "packages.listFiles",
				       G_TYPE_STRING, rhn->priv->session_key,
				       G_TYPE_INT, rhn->priv->id,
				       G_TYPE_INVALID);

	/* send message */
	soup_session_send_message (rhn->priv->session, msg);
	ret = SOUP_STATUS_IS_SUCCESSFUL (msg->status_code);
	if (!ret) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "%s (error #%d)",
			     msg->reason_phrase,
			     msg->status_code);
		goto out;
	}

	/* get response */
	ret = soup_xmlrpc_extract_method_response (msg->response_body->data,
						   msg->response_body->length,
						   &error_local,
						   G_TYPE_ARRAY, &array);
	if (!ret) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "Could not parse XML-RPC response for %s (%i): %s",
			     msg->response_body->data,
			     (guint) msg->response_body->length,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get packages */
	files = g_ptr_array_new_with_free_func (g_free);
	for (i=0; i<array->len; i++) {
		hash = g_array_index (array, GHashTable *, i);

		/* FIXME: do we care that we're adding directories? */
		value = g_hash_table_lookup (hash, "file_path");
		g_ptr_array_add (files, g_value_dup_string (value));
	}

	/* set files */
	zif_package_set_files (pkg, files);
out:
	if (array != NULL)
		g_array_free (array, TRUE);
	if (files != NULL)
		g_ptr_array_unref (files);
	g_object_unref (msg);
	return ret;
}

/*
 * zif_package_rhn_depend_parse:
 */
static gboolean
zif_package_rhn_depend_parse (ZifDepend *depend,
			      const gchar *name,
			      const gchar *modifier,
			      GError **error)
{
	gboolean ret;
	gchar *tmp = NULL;

	/* append if the modifier exists */
	if (modifier != NULL &&
	    modifier[0] != '\0' &&
	    modifier[0] != ' ') {
		tmp = g_strdup_printf ("%s %s", name, modifier);
		ret = zif_depend_parse_description (depend, tmp, error);
	} else {
		ret = zif_depend_parse_description (depend, name, error);
	}

	g_free (tmp);
	return ret;
}

/*
 * zif_package_rhn_list_deps:
 */
static gboolean
zif_package_rhn_list_deps (ZifPackageRhn *rhn,
			   ZifState *state,
			   GError **error)
{
	const gchar *type;
	gboolean ret;
	GError *error_local = NULL;
	GHashTable *hash;
	GPtrArray *conflicts = NULL;
	GPtrArray *obsoletes = NULL;
	GPtrArray *provides = NULL;
	GPtrArray *requires = NULL;
	guint i;
	GArray *array = NULL;
	GValue *modifier;
	GValue *name;
	GValue *value;
	SoupMessage *msg;
	ZifDepend *depend;
	ZifPackage *pkg = ZIF_PACKAGE (rhn);

	/* create request */
	msg = soup_xmlrpc_request_new (rhn->priv->server,
				       "packages.listDependencies",
				       G_TYPE_STRING, rhn->priv->session_key,
				       G_TYPE_INT, rhn->priv->id,
				       G_TYPE_INVALID);

	/* send message */
	soup_session_send_message (rhn->priv->session, msg);
	ret = SOUP_STATUS_IS_SUCCESSFUL (msg->status_code);
	if (!ret) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "%s (error #%d)",
			     msg->reason_phrase,
			     msg->status_code);
		goto out;
	}

	/* get response */
	ret = soup_xmlrpc_extract_method_response (msg->response_body->data,
						   msg->response_body->length,
						   &error_local,
						   G_TYPE_ARRAY, &array);
	if (!ret) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "Could not parse XML-RPC response for %s (%i): %s",
			     msg->response_body->data,
			     (guint) msg->response_body->length,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get packages */
	provides = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	requires = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	obsoletes = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	conflicts = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array->len; i++) {
		hash = g_array_index (array, GHashTable *, i);

		value = g_hash_table_lookup (hash, "dependency_type");
		type = g_value_get_string (value);

		/* create a new depend object */
		depend = zif_depend_new ();
		name = g_hash_table_lookup (hash, "dependency");
		modifier = g_hash_table_lookup (hash, "dependency_modifier");
		ret = zif_package_rhn_depend_parse (depend,
						    g_value_get_string (name),
						    g_value_get_string (modifier),
						    error);
		g_assert_no_error (*error);
		g_assert (ret);
		if (!ret) {
			g_object_unref (depend);
			goto out;
		}

		if (g_strcmp0 (type, "provides") == 0) {
			g_ptr_array_add (provides, depend);
		} else if (g_strcmp0 (type, "requires") == 0) {
			g_ptr_array_add (requires, depend);
		} else if (g_strcmp0 (type, "obsoletes") == 0) {
			g_ptr_array_add (obsoletes, depend);
		} else if (g_strcmp0 (type, "conflicts") == 0) {
			g_ptr_array_add (conflicts, depend);
		} else {
			g_assert_not_reached ();
		}
	}

	/* set files */
	zif_package_set_provides (pkg, provides);
	zif_package_set_requires (pkg, requires);
	zif_package_set_obsoletes (pkg, obsoletes);
	zif_package_set_conflicts (pkg, conflicts);
out:
	if (array != NULL)
		g_array_free (array, TRUE);
	if (provides != NULL)
		g_ptr_array_unref (provides);
	if (requires != NULL)
		g_ptr_array_unref (requires);
	if (obsoletes != NULL)
		g_ptr_array_unref (obsoletes);
	if (conflicts != NULL)
		g_ptr_array_unref (conflicts);
	g_object_unref (msg);
	return ret;
}

/*
 * zif_package_rhn_ensure_data:
 */
static gboolean
zif_package_rhn_ensure_data (ZifPackage *pkg,
			     ZifPackageEnsureType type,
			     ZifState *state,
			     GError **error)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	switch (type) {
	case ZIF_PACKAGE_ENSURE_TYPE_DESCRIPTION:
	case ZIF_PACKAGE_ENSURE_TYPE_LICENCE:
	case ZIF_PACKAGE_ENSURE_TYPE_SIZE:
	case ZIF_PACKAGE_ENSURE_TYPE_SUMMARY:
	case ZIF_PACKAGE_ENSURE_TYPE_GROUP:
	case ZIF_PACKAGE_ENSURE_TYPE_CACHE_FILENAME:
	case ZIF_PACKAGE_ENSURE_TYPE_CATEGORY:
	case ZIF_PACKAGE_ENSURE_TYPE_URL:
		ret = zif_package_rhn_get_details (ZIF_PACKAGE_RHN (pkg),
						   state,
						   error);
		if (!ret)
			goto out;
		break;
	case ZIF_PACKAGE_ENSURE_TYPE_FILES:
		ret = zif_package_rhn_list_files (ZIF_PACKAGE_RHN (pkg),
						  state,
						  error);
		if (!ret)
			goto out;
		break;
	case ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS:
	case ZIF_PACKAGE_ENSURE_TYPE_PROVIDES:
	case ZIF_PACKAGE_ENSURE_TYPE_REQUIRES:
	case ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES:
		ret = zif_package_rhn_list_deps (ZIF_PACKAGE_RHN (pkg),
						 state,
						 error);
		if (!ret)
			goto out;
		break;
	default:
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_NO_SUPPORT,
			     "Ensure type '%s' not supported on ZifPackageRhn",
			     zif_package_ensure_type_to_string (type));
		break;
	}
out:
	return ret;
}

/*
 * zif_package_rhn_precache:
 */
gboolean
zif_package_rhn_precache (ZifPackageRhn *rhn,
			  ZifPackageRhnPrecache precache,
			  GError **error)
{
	gboolean ret = TRUE;

	/* get details */
	if ((precache & ZIF_PACKAGE_RHN_PRECACHE_GET_DETAILS) > 0) {
		ret = zif_package_rhn_get_details (rhn, NULL, error);
		if (!ret)
			goto out;
	}

	/* list files */
	if ((precache & ZIF_PACKAGE_RHN_PRECACHE_LIST_FILES) > 0) {
		ret = zif_package_rhn_list_files (rhn, NULL, error);
		if (!ret)
			goto out;
	}

	/* list deps */
	if ((precache & ZIF_PACKAGE_RHN_PRECACHE_LIST_DEPS) > 0) {
		ret = zif_package_rhn_list_deps (rhn, NULL, error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * zif_package_rhn_get_id:
 * @pkg: A #ZifPackageRhn
 *
 * Gets the RHN package ID for the package.
 *
 * Return value: The value or 0 if unset
 *
 * Since: 0.1.6
 **/
guint
zif_package_rhn_get_id (ZifPackageRhn *pkg)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE_RHN (pkg), 0);
	return pkg->priv->id;
}

/**
 * zif_package_rhn_set_id:
 * @pkg: A #ZifPackageRhn
 * @id: the RHN package ID
 *
 * Sets a RHN package ID.
 *
 * Since: 0.1.6
 **/
void
zif_package_rhn_set_id (ZifPackageRhn *pkg, guint id)
{
	g_return_if_fail (ZIF_IS_PACKAGE_RHN (pkg));
	g_return_if_fail (id != 0);
	pkg->priv->id = id;
}

/**
 * zif_package_rhn_set_session_key:
 * @pkg: A #ZifPackageRhn
 * @session_key: the RHN session key
 *
 * Sets a RHN session key.
 *
 * Since: 0.1.6
 **/
void
zif_package_rhn_set_session_key (ZifPackageRhn *pkg,
				 const gchar *session_key)
{
	g_return_if_fail (ZIF_IS_PACKAGE_RHN (pkg));
	g_return_if_fail (session_key != NULL);
	g_free (pkg->priv->session_key);
	pkg->priv->session_key = g_strdup (session_key);
}

/**
 * zif_package_rhn_set_server:
 * @pkg: A #ZifPackageRhn
 * @server: the RHN server to use
 *
 * Sets a RHN server.
 *
 * Since: 0.1.6
 **/
void
zif_package_rhn_set_server (ZifPackageRhn *pkg,
			    const gchar *server)
{
	g_return_if_fail (ZIF_IS_PACKAGE_RHN (pkg));
	g_return_if_fail (server != NULL);
	g_free (pkg->priv->server);
	pkg->priv->server = g_strdup (server);
}

/**
 * zif_package_rhn_finalize:
 **/
static void
zif_package_rhn_finalize (GObject *object)
{
	ZifPackageRhn *pkg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_PACKAGE_RHN (object));
	pkg = ZIF_PACKAGE_RHN (object);

	g_free (pkg->priv->session_key);
	g_free (pkg->priv->server);
	g_object_unref (pkg->priv->session);

	G_OBJECT_CLASS (zif_package_rhn_parent_class)->finalize (object);
}

/**
 * zif_package_rhn_class_init:
 **/
static void
zif_package_rhn_class_init (ZifPackageRhnClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifPackageClass *package_class = ZIF_PACKAGE_CLASS (klass);
	object_class->finalize = zif_package_rhn_finalize;

	package_class->ensure_data = zif_package_rhn_ensure_data;

	g_type_class_add_private (klass, sizeof (ZifPackageRhnPrivate));
}

/**
 * zif_package_rhn_init:
 **/
static void
zif_package_rhn_init (ZifPackageRhn *pkg)
{
	pkg->priv = ZIF_PACKAGE_RHN_GET_PRIVATE (pkg);
	pkg->priv->session = soup_session_sync_new ();
}

/**
 * zif_package_rhn_new:
 *
 * Return value: A new #ZifPackageRhn instance.
 *
 * Since: 0.1.6
 **/
ZifPackage *
zif_package_rhn_new (void)
{
	ZifPackage *pkg;
	pkg = g_object_new (ZIF_TYPE_PACKAGE_RHN, NULL);
	return ZIF_PACKAGE (pkg);
}
