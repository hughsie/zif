/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010-2011 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-manifest
 * @short_description: Parse and run .manifest files.
 *
 * A manifest file is a file that describes a transaction and optionally
 * details the pre and post system state.
 * It is used to verify results of #ZifTransaction.
 * A manifest file looks like:
 *
 * config
 * 	archinfo=i386
 *
 * local
 * 	hal;0.0.1-1;i386;meta
 *
 * remote
 * 	hal;0.0.2-1;i386;meta
 *
 * transaction
 * 	install
 * 		hal
 *
 * result
 * 	hal;0.0.2-1;i386;meta
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <libsoup/soup.h>

#include "zif-config.h"
#include "zif-manifest.h"
#include "zif-object-array.h"
#include "zif-package-array.h"
#include "zif-package-meta.h"
#include "zif-package-private.h"
#include "zif-state.h"
#include "zif-store-array.h"
#include "zif-store-meta.h"
#include "zif-transaction.h"
#include "zif-transaction-private.h"
#include "zif-utils.h"

#define ZIF_MANIFEST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MANIFEST, ZifManifestPrivate))

/**
 * ZifManifestPrivate:
 *
 * Private #ZifManifest data
 **/
struct _ZifManifestPrivate
{
	ZifConfig		*config;
	gboolean		 write_history;
};

typedef enum {
	ZIF_MANIFEST_SECTION_CONFIG,
	ZIF_MANIFEST_SECTION_LOCAL,
	ZIF_MANIFEST_SECTION_REMOTE,
	ZIF_MANIFEST_SECTION_TRANSACTION,
	ZIF_MANIFEST_SECTION_RESULT,
	ZIF_MANIFEST_SECTION_UNKNOWN
} ZifManifestSection;

typedef enum {
	ZIF_MANIFEST_RESOURCE_REQUIRES,
	ZIF_MANIFEST_RESOURCE_PROVIDES,
	ZIF_MANIFEST_RESOURCE_CONFLICTS,
	ZIF_MANIFEST_RESOURCE_OBSOLETES,
	ZIF_MANIFEST_RESOURCE_FILES,
	ZIF_MANIFEST_RESOURCE_SRPM,
	ZIF_MANIFEST_RESOURCE_UNKNOWN
} ZifManifestResource;

typedef enum {
	ZIF_MANIFEST_ACTION_INSTALL,
	ZIF_MANIFEST_ACTION_INSTALL_AS_UPDATE,
	ZIF_MANIFEST_ACTION_UPDATE,
	ZIF_MANIFEST_ACTION_REMOVE,
	ZIF_MANIFEST_ACTION_GET_UPDATES,
	ZIF_MANIFEST_ACTION_DOWNGRADE,
	ZIF_MANIFEST_ACTION_UNKNOWN
} ZifManifestAction;

G_DEFINE_TYPE (ZifManifest, zif_manifest, G_TYPE_OBJECT)

/**
 * zif_manifest_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.1.3
 **/
GQuark
zif_manifest_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_manifest_error");
	return quark;
}

/**
 * zif_manifest_section_from_string:
 **/
static ZifManifestSection
zif_manifest_section_from_string (const gchar *section)
{
	if (g_strcmp0 (section, "config") == 0)
		return ZIF_MANIFEST_SECTION_CONFIG;
	if (g_strcmp0 (section, "local") == 0)
		return ZIF_MANIFEST_SECTION_LOCAL;
	if (g_strcmp0 (section, "remote") == 0)
		return ZIF_MANIFEST_SECTION_REMOTE;
	if (g_strcmp0 (section, "transaction") == 0)
		return ZIF_MANIFEST_SECTION_TRANSACTION;
	if (g_strcmp0 (section, "result") == 0)
		return ZIF_MANIFEST_SECTION_RESULT;
	return ZIF_MANIFEST_SECTION_UNKNOWN;
}

/**
 * zif_manifest_resource_from_string:
 **/
static ZifManifestResource
zif_manifest_resource_from_string (const gchar *section)
{
	if (g_strcmp0 (section, "Requires") == 0)
		return ZIF_MANIFEST_RESOURCE_REQUIRES;
	if (g_strcmp0 (section, "Provides") == 0)
		return ZIF_MANIFEST_RESOURCE_PROVIDES;
	if (g_strcmp0 (section, "Conflicts") == 0)
		return ZIF_MANIFEST_RESOURCE_CONFLICTS;
	if (g_strcmp0 (section, "Obsoletes") == 0)
		return ZIF_MANIFEST_RESOURCE_OBSOLETES;
	if (g_strcmp0 (section, "Files") == 0)
		return ZIF_MANIFEST_RESOURCE_FILES;
	if (g_strcmp0 (section, "Srpm") == 0)
		return ZIF_MANIFEST_RESOURCE_SRPM;
	return ZIF_MANIFEST_RESOURCE_UNKNOWN;
}

/**
 * zif_manifest_action_from_string:
 **/
static ZifManifestAction
zif_manifest_action_from_string (const gchar *section)
{
	if (g_strcmp0 (section, "install") == 0)
		return ZIF_MANIFEST_ACTION_INSTALL;
	if (g_strcmp0 (section, "update") == 0)
		return ZIF_MANIFEST_ACTION_UPDATE;
	if (g_strcmp0 (section, "install-as-update") == 0)
		return ZIF_MANIFEST_ACTION_INSTALL_AS_UPDATE;
	if (g_strcmp0 (section, "remove") == 0)
		return ZIF_MANIFEST_ACTION_REMOVE;
	if (g_strcmp0 (section, "get-updates") == 0)
		return ZIF_MANIFEST_ACTION_GET_UPDATES;
	if (g_strcmp0 (section, "downgrade") == 0)
		return ZIF_MANIFEST_ACTION_DOWNGRADE;
	return ZIF_MANIFEST_ACTION_UNKNOWN;
}

/**
 * zif_manifest_add_package_to_store:
 **/
static gboolean
zif_manifest_add_package_to_store (ZifManifest *manifest,
				   ZifStore *store,
				   ZifPackage *package,
				   GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	/* add to store */
	ret = zif_store_add_package (store, package, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_MANIFEST_ERROR,
			     ZIF_MANIFEST_ERROR_POST_INSTALL,
			     "Failed to add package %s: %s",
			     zif_package_get_printable (package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	return ret;
}

/**
 * zif_manifest_add_package_to_transaction:
 **/
static gboolean
zif_manifest_add_package_to_transaction (ZifManifest *manifest,
					 ZifTransaction *transaction,
					 ZifStore *store,
					 ZifManifestAction action,
					 const gchar *package_id,
					 ZifState *state,
					 GError **error)
{
	ZifPackage *package = NULL;
	gboolean ret = FALSE;
	GError *error_local = NULL;
	const gchar *to_array[] = { NULL, NULL };
	GPtrArray *package_array = NULL;

	if (zif_package_id_check (package_id)) {
		/* get metapackage */
		package = zif_store_find_package (store,
						  package_id,
						  state,
						  &error_local);
		if (package == NULL) {
			g_set_error (error,
				     ZIF_MANIFEST_ERROR,
				     ZIF_MANIFEST_ERROR_POST_INSTALL,
				     "Failed to add package %s to store %s: %s",
				     package_id,
				     zif_store_get_id (store),
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	} else {

		/* search store for package */
		to_array[0] = package_id;
		package_array = zif_store_resolve (store,
						   (gchar **) to_array,
						   state,
						   error);
		if (package_array == NULL)
			goto out;

		/* nothing found */
		if (package_array->len == 0) {
			g_set_error (error,
				     ZIF_MANIFEST_ERROR,
				     ZIF_MANIFEST_ERROR_POST_INSTALL,
				     "no item %s found in %s",
				     package_id,
				     zif_store_get_id (store));
			goto out;
		}

		/* ambiguous */
		if (package_array->len > 1) {
			g_debug ("more than one item %s found in %s, "
				 "so choosing newest",
				 package_id,
				 zif_store_get_id (store));
			package = zif_package_array_get_newest (package_array,
								error);
			if (package == NULL)
				goto out;
		} else {
			/* one item, yay */
			package = g_object_ref (g_ptr_array_index (package_array, 0));
		}
	}

	/* add it to the transaction */
	if (action == ZIF_MANIFEST_ACTION_INSTALL)
		ret = zif_transaction_add_install (transaction, package, &error_local);
	else if (action == ZIF_MANIFEST_ACTION_REMOVE)
		ret = zif_transaction_add_remove (transaction, package, &error_local);
	else if (action == ZIF_MANIFEST_ACTION_UPDATE)
		ret = zif_transaction_add_update (transaction, package, &error_local);
	else if (action == ZIF_MANIFEST_ACTION_INSTALL_AS_UPDATE)
		ret = zif_transaction_add_install_as_update (transaction, package, &error_local);
	else if (action == ZIF_MANIFEST_ACTION_DOWNGRADE)
		ret = zif_transaction_add_install_as_downgrade (transaction, package, &error_local);
	else
		g_assert_not_reached ();
	if (!ret) {
		g_set_error (error,
			     ZIF_MANIFEST_ERROR,
			     ZIF_MANIFEST_ERROR_POST_INSTALL,
			     "Failed to add package to transaction %s: %s",
			     zif_package_get_printable (package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	if (package_array != NULL)
		g_ptr_array_unref (package_array);
	if (package != NULL)
		g_object_unref (package);
	return ret;
}

/**
 * zif_manifest_check_array:
 **/
static gboolean
zif_manifest_check_array (GPtrArray *array,
			  GPtrArray *packages,
			  GError **error)
{
	guint i;
	gboolean ret = FALSE;
	ZifState *state;
	ZifPackage *package;
	ZifPackage *package_tmp;

	/* find each package */
	state = zif_state_new ();
	for (i=0; i < packages->len; i++) {

		package_tmp = g_ptr_array_index (packages, i);

		zif_state_reset (state);
		package = zif_package_array_find (array,
						  zif_package_get_id (package_tmp),
						  error);
		if (package == NULL)
			goto out;
		g_object_unref (package);
	}

	/* ensure same size */
	if (packages->len != array->len) {
		g_set_error (error,
			     ZIF_MANIFEST_ERROR,
			     ZIF_MANIFEST_ERROR_POST_INSTALL,
			     "post action database wrong size %i when supposed to be %i",
			     array->len, packages->len);
		g_debug ("listing files in store");
		for (i = 0; i < array->len; i++) {
			package = g_ptr_array_index (array, i);
			g_debug ("%i.\t%s", i+1, zif_package_get_printable (package));
		}
		goto out;
	}

	/* success */
	ret= TRUE;
out:
	g_object_unref (state);
	return ret;
}

/**
 * zif_manifest_check_post_installed:
 **/
static gboolean
zif_manifest_check_post_installed (ZifManifest *manifest,
				   ZifStore *store,
				   GPtrArray *packages,
				   GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	ZifState *state;

	/* ensure same size */
	state = zif_state_new ();
	array = zif_store_get_packages (store, state, error);
	if (array == NULL)
		goto out;
	ret = zif_manifest_check_array (array, packages, error);
	if (!ret)
		goto out;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	g_object_unref (state);
	return ret;
}

/**
 * zif_manifest_set_config:
 **/
static gboolean
zif_manifest_set_config (ZifManifest *manifest,
			 const gchar *config,
			 GError **error)
{
	gboolean ret = TRUE;
	gchar **vars;

	/* each option */
	vars = g_strsplit (config, "=", 2);
	zif_config_unset (manifest->priv->config, vars[0], NULL);
	g_debug ("config %s=%s", vars[0], vars[1]);
	if (g_strcmp0 (vars[0], "history_db") == 0)
		manifest->priv->write_history = TRUE;
	ret = zif_config_set_string (manifest->priv->config,
				     vars[0], vars[1],
				     error);
	g_strfreev (vars);
	return ret;
}

/**
 * zif_manifest_add_resource_to_package:
 **/
static gboolean
zif_manifest_add_resource_to_package (ZifPackage *package,
				      ZifManifestResource resource,
				      const gchar *resource_description,
				      GError **error)
{
	gboolean ret = FALSE;
	ZifDepend *depend = NULL;
	ZifString *str = NULL;

	/* not yet set */
	if (package == NULL) {
		g_set_error_literal (error,
				     ZIF_MANIFEST_ERROR,
				     ZIF_MANIFEST_ERROR_FAILED,
				     "no package yet!");
		goto out;
	}
	if (resource == ZIF_MANIFEST_RESOURCE_UNKNOWN) {
		g_set_error_literal (error,
				     ZIF_MANIFEST_ERROR,
				     ZIF_MANIFEST_ERROR_FAILED,
				     "no depend type yet!");
		goto out;
	}

	/* parse if it's a depend */
	switch (resource) {
	case ZIF_MANIFEST_RESOURCE_REQUIRES:
	case ZIF_MANIFEST_RESOURCE_CONFLICTS:
	case ZIF_MANIFEST_RESOURCE_OBSOLETES:
	case ZIF_MANIFEST_RESOURCE_PROVIDES:
		depend = zif_depend_new ();
		ret = zif_depend_parse_description (depend,
						    resource_description,
						    error);
		if (!ret)
			goto out;
		break;
	default:
		ret = TRUE;
		break;
	}

	if (resource == ZIF_MANIFEST_RESOURCE_REQUIRES) {
		zif_package_add_require (package,
					 depend);
	} else if (resource == ZIF_MANIFEST_RESOURCE_CONFLICTS) {
		zif_package_add_conflict (package,
					  depend);
	} else if (resource == ZIF_MANIFEST_RESOURCE_OBSOLETES) {
		zif_package_add_obsolete (package,
					  depend);
	} else if (resource == ZIF_MANIFEST_RESOURCE_PROVIDES) {
		zif_package_add_provide (package,
					 depend);
	} else if (resource == ZIF_MANIFEST_RESOURCE_FILES) {
		zif_package_add_file (package,
				      resource_description);
	} else if (resource == ZIF_MANIFEST_RESOURCE_SRPM) {
		str = zif_string_new (resource_description);
		zif_package_set_source_filename (package, str);
	} else {
		g_assert_not_reached ();
	}
out:
	if (str != NULL)
		zif_string_unref (str);
	if (depend != NULL)
		g_object_unref (depend);
	return ret;
}

/**
 * zif_manifest_check_section:
 **/
static gboolean
zif_manifest_check_section (ZifManifest *manifest,
			    const gchar *data,
			    ZifState *state,
			    GError **error)
{
	const gchar *tmp;
	gboolean ret;
	gchar **lines = NULL;
	GError *error_local = NULL;
	GPtrArray *remote_array = NULL;
	GPtrArray *resolve_install = NULL;
	GPtrArray *resolve_remove = NULL;
	GPtrArray *result_array = NULL;
	guint i;
	guint level;
	ZifManifestResource resource = ZIF_MANIFEST_RESOURCE_UNKNOWN;
	ZifManifestSection section = ZIF_MANIFEST_SECTION_UNKNOWN;
	ZifManifestAction action = ZIF_MANIFEST_ACTION_UNKNOWN;
	ZifPackage *package = NULL;
	ZifState *state_local;
	ZifState *state_loop;
	ZifStore *local = NULL;
	ZifStore *remote = NULL;
	ZifStore *store_hint;
	ZifTransaction *transaction = NULL;

	/* setup steps */
	ret = zif_state_set_steps (state,
				   error,
				   10, /* parse */
				   80, /* resolve packages */
				   10, /* check */
				   -1);
	if (!ret)
		goto out;

	/* create virtual stores */
	local = zif_store_meta_new ();
	zif_store_meta_set_is_local (ZIF_STORE_META (local), TRUE);
	remote = zif_store_meta_new ();
	remote_array = zif_store_array_new ();
	zif_store_array_add_store (remote_array, remote);
	result_array = zif_object_array_new ();

	/* setup transaction */
	transaction = zif_transaction_new ();
	zif_transaction_set_verbose (transaction, TRUE);
	zif_transaction_set_store_local (transaction, local);
	zif_transaction_set_stores_remote (transaction, remote_array);

	state_local = zif_state_get_child (state);
	lines = g_strsplit (data, "\n", -1);
	zif_state_set_number_steps (state_local, g_strv_length (lines));
	for (i=0; lines[i] != NULL; i++) {

		if (lines[i][0] == '\0')
			goto skip;
		if (lines[i][0] == '#')
			goto skip;

		/* special command */
		if (g_strcmp0 (lines[i], "disable") == 0) {
			g_debug ("Skipping as disabled");
			ret = zif_state_finished (state, error);
			goto out;
		}

		/* find curent line level */
		for (level=0; lines[i][level] != '\0'; level++) {
			if (lines[i][level] != '\t')
				break;
		}
		if (level > 3) {
			ret = FALSE;
			g_set_error (error,
				     ZIF_MANIFEST_ERROR,
				     ZIF_MANIFEST_ERROR_FAILED,
				     "too much indentation '%s'",
				     lines[i]);
			goto out;
		}

		/* parse the tree */
		tmp = lines[i] + level;
		if (tmp[0] == '\0')
			goto skip;
		g_debug ("ln %i, level=%i, data=%s", i, level, tmp);
		if (level == 0) {
			section = zif_manifest_section_from_string (tmp);
			if (section == ZIF_MANIFEST_SECTION_UNKNOWN) {
				ret = FALSE;
				g_set_error (error,
					     ZIF_MANIFEST_ERROR,
					     ZIF_MANIFEST_ERROR_FAILED,
					     "unknown section '%s'",
					     tmp);
				goto out;
			}
		} else if (level == 1) {
			if (section == ZIF_MANIFEST_SECTION_CONFIG) {
				ret = zif_manifest_set_config (manifest,
							       tmp,
							       error);
				if (!ret)
					goto out;
			} else if (section == ZIF_MANIFEST_SECTION_LOCAL) {
				package = zif_package_meta_new ();
				ret = zif_package_set_id (package, tmp, error);
				if (!ret)
					goto out;
				ret = zif_manifest_add_package_to_store (manifest,
									 local,
									 package,
									 error);
				if (!ret)
					goto out;
			} else if (section == ZIF_MANIFEST_SECTION_REMOTE) {
				package = zif_package_meta_new ();
				ret = zif_package_set_id (package, tmp, error);
				if (!ret)
					goto out;
				ret = zif_manifest_add_package_to_store (manifest,
									 remote,
									 package,
									 error);
				if (!ret)
					goto out;
			} else if (section == ZIF_MANIFEST_SECTION_RESULT) {
				package = zif_package_new ();
				ret = zif_package_set_id (package, tmp, error);
				if (!ret)
					goto out;
				g_ptr_array_add (result_array, package);
			} else if (section == ZIF_MANIFEST_SECTION_TRANSACTION) {
				action = zif_manifest_action_from_string (tmp);
				if (action == ZIF_MANIFEST_ACTION_UNKNOWN) {
					ret = FALSE;
					g_set_error (error,
						     ZIF_MANIFEST_ERROR,
						     ZIF_MANIFEST_ERROR_FAILED,
						     "unknown transaction kind '%s'",
						     tmp);
					goto out;
				}
			} else {
				ret = FALSE;
				g_set_error (error,
					     ZIF_MANIFEST_ERROR,
					     ZIF_MANIFEST_ERROR_FAILED,
					     "unexpected subcommand '%s'",
					     tmp);
				goto out;
			}
		} else if (level == 2) {
			if (section == ZIF_MANIFEST_SECTION_LOCAL ||
			    section == ZIF_MANIFEST_SECTION_REMOTE) {
				resource = zif_manifest_resource_from_string (tmp);
				if (resource == ZIF_MANIFEST_RESOURCE_UNKNOWN) {
					ret = FALSE;
					g_set_error (error,
						     ZIF_MANIFEST_ERROR,
						     ZIF_MANIFEST_ERROR_FAILED,
						     "unknown depend kind '%s'",
						     tmp);
					goto out;
				}
			} else if (section == ZIF_MANIFEST_SECTION_TRANSACTION) {
				if (action == ZIF_MANIFEST_ACTION_INSTALL ||
				    action == ZIF_MANIFEST_ACTION_DOWNGRADE ||
				    action == ZIF_MANIFEST_ACTION_INSTALL_AS_UPDATE) {
					store_hint = remote;
				} else {
					store_hint = local;
				}
				state_loop = zif_state_get_child (state_local);
				ret = zif_manifest_add_package_to_transaction (manifest,
									       transaction,
									       store_hint,
									       action,
									       tmp,
									       state_loop,
									       error);
				if (!ret)
					goto out;
			} else {
				ret = FALSE;
				g_set_error (error,
					     ZIF_MANIFEST_ERROR,
					     ZIF_MANIFEST_ERROR_FAILED,
					     "unexpected subsubcommand '%s'",
					     tmp);
				goto out;
			}
		} else if (level == 3) {
			if (section == ZIF_MANIFEST_SECTION_LOCAL ||
			    section == ZIF_MANIFEST_SECTION_REMOTE) {
				ret = zif_manifest_add_resource_to_package (package,
									    resource,
									    tmp,
									    error);
				if (!ret)
					goto out;
			} else {
				ret = FALSE;
				g_set_error (error,
					     ZIF_MANIFEST_ERROR,
					     ZIF_MANIFEST_ERROR_FAILED,
					     "syntax error '%s'",
					     tmp);
				goto out;
			}
		} else {
			g_assert_not_reached ();
		}
skip:
		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* treat get-updates specially */
	if (action == ZIF_MANIFEST_ACTION_GET_UPDATES) {
		state_local = zif_state_get_child (state);
		resolve_install = zif_store_array_get_updates (remote_array,
							       local,
							       state_local,
							       error);
		if (resolve_install == NULL) {
			ret = FALSE;
			goto out;
		}
		ret = zif_manifest_check_array (resolve_install, result_array, error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_finished (state, error);
		if (!ret)
			goto out;

		goto out;
	}

	/* resolve */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_resolve (transaction, state_local, &error_local);
	if (!ret) {
		/* this is special */
		if (error_local->code == ZIF_TRANSACTION_ERROR_NOTHING_TO_DO) {
			g_clear_error (&error_local);
			ret = zif_state_finished (state_local, error);
			if (!ret)
				goto out;
		} else {
			g_set_error (error,
				     ZIF_MANIFEST_ERROR,
				     ZIF_MANIFEST_ERROR_FAILED,
				     "failed to resolve transaction: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* add the output of the resolve to the fake local repo */
	resolve_install = zif_transaction_get_install (transaction);
	ret = zif_store_add_packages (local, resolve_install, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_MANIFEST_ERROR,
			     ZIF_MANIFEST_ERROR_FAILED,
			     "failed to add transaction set to local store: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* remove the output of the resolve to the fake local repo */
	resolve_remove = zif_transaction_get_remove (transaction);
	ret = zif_store_remove_packages (local, resolve_remove, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_MANIFEST_ERROR,
			     ZIF_MANIFEST_ERROR_FAILED,
			     "failed to remove transaction set from local store: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check state */
	if (result_array != NULL) {
		ret = zif_manifest_check_post_installed (manifest,
							 local,
							 result_array,
							 error);
		if (!ret)
			goto out;
	} else {
		g_warning ("result usually required...");
	}

	/* write history */
	if (manifest->priv->write_history) {
		g_debug ("writing history");
		ret = zif_transaction_write_history (transaction, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_strfreev (lines);
	if (local != NULL)
		g_object_unref (local);
	if (remote != NULL)
		g_object_unref (remote);
	if (transaction != NULL)
		g_object_unref (transaction);
	if (remote_array != NULL)
		g_ptr_array_unref (remote_array);
	if (result_array != NULL)
		g_ptr_array_unref (result_array);
	if (resolve_install != NULL)
		g_ptr_array_unref (resolve_install);
	if (resolve_remove != NULL)
		g_ptr_array_unref (resolve_remove);
	return ret;
}

/**
 * zif_manifest_check:
 * @manifest: A #ZifManifest
 * @filename: A maifest file to use
 * @error: A #GError, or %NULL
 *
 * Resolves and checks a transaction.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_manifest_check (ZifManifest *manifest,
		    const gchar *filename,
		    ZifState *state,
		    GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	gchar **sections = NULL;
	guint i;
	ZifState *state_local;

	g_return_val_if_fail (ZIF_IS_MANIFEST (manifest), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* load file */
	g_debug ("             ---            ");
	g_debug ("loading manifest %s", filename);

	/* reset so the history enable is per-file, not per-instance */
	manifest->priv->write_history = FALSE;

	/* parse file */
	ret = g_file_get_contents (filename, &data, NULL, error);
	if (!ret)
		goto out;

	/* parse each section */
	sections = g_strsplit (data, "flush\n", -1);
	zif_state_set_number_steps (state, g_strv_length (sections));
	for (i = 0; sections[i] != NULL; i++) {

		/* parse this chunk */
		state_local = zif_state_get_child (state);
		ret = zif_manifest_check_section (manifest,
						  sections[i],
						  state_local,
						  error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}
out:
	g_strfreev (sections);
	g_free (data);
	return ret;
}

/**
 * zif_manifest_finalize:
 **/
static void
zif_manifest_finalize (GObject *object)
{
	ZifManifest *manifest;
	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MANIFEST (object));
	manifest = ZIF_MANIFEST (object);
	g_object_unref (manifest->priv->config);
	G_OBJECT_CLASS (zif_manifest_parent_class)->finalize (object);
}

/**
 * zif_manifest_class_init:
 **/
static void
zif_manifest_class_init (ZifManifestClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_manifest_finalize;

	g_type_class_add_private (klass, sizeof (ZifManifestPrivate));
}

/**
 * zif_manifest_init:
 **/
static void
zif_manifest_init (ZifManifest *manifest)
{
	manifest->priv = ZIF_MANIFEST_GET_PRIVATE (manifest);
	manifest->priv->config = zif_config_new ();
}

/**
 * zif_manifest_new:
 *
 * Return value: A new manifest instance.
 *
 * Since: 0.1.3
 **/
ZifManifest *
zif_manifest_new (void)
{
	ZifManifest *manifest;
	manifest = g_object_new (ZIF_TYPE_MANIFEST, NULL);
	return ZIF_MANIFEST (manifest);
}

