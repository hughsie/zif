/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Some ideas have been taken from:
 *
 * yum, Copyright (C) 2002 - 2010 Seth Vidal <skvidal@fedoraproject.org>
 * low, Copyright (C) 2008 - 2010 James Bowes <jbowes@repl.ca>
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
 * SECTION:zif-transaction
 * @short_description: A #ZifTransaction object represents a package action.
 *
 * #ZifTransaction allows the user to add install, update and remove actions
 * to be written to disk.
 *
 * This is the dependency resolution algorithm used in Zif (like in YUM).
 *
 * The Algorithm
 * - WHILE there are unresolved dependencies DO:
 *   - FOR EACH package to be installed DO:
 *     - FOR EACH requires of the package DO:
 *       - IF NOT requires provided by installed packages
 *         OR NOT requires provided by packages in the transaction DO:
 *         - Add requires to unresolved requires.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <fcntl.h>

#include <rpm/rpmdb.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmps.h>
#include <rpm/rpmts.h>

#include "zif-array.h"
#include "zif-config.h"
#include "zif-depend.h"
#include "zif-object-array.h"
#include "zif-package-array.h"
#include "zif-package-meta.h"
#include "zif-package-local.h"
#include "zif-package-remote.h"
#include "zif-store-array.h"
#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-transaction.h"

#define ZIF_TRANSACTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_TRANSACTION, ZifTransactionPrivate))

struct _ZifTransactionPrivate
{
	GPtrArray		*install;
	GPtrArray		*update;
	GPtrArray		*remove;
	GHashTable		*install_hash;
	GHashTable		*update_hash;
	GHashTable		*remove_hash;
	ZifStore		*store_local;
	ZifConfig		*config;
	GPtrArray		*stores_remote;
	gboolean		 verbose;
	ZifTransactionState	 state;
};

typedef struct {
	ZifState		*state;
	ZifTransaction		*transaction;
	gboolean		 unresolved_dependencies;
	ZifArray		*post_resolve_package_array;
} ZifTransactionResolve;

G_DEFINE_TYPE (ZifTransaction, zif_transaction, G_TYPE_OBJECT)

typedef struct {
	ZifPackage		*package;
	GPtrArray		*related_packages; /* allows us to remove deps if the parent failed */
	gboolean		 resolved;
	gboolean		 cancelled;
	ZifTransactionReason	 reason;
} ZifTransactionItem;

/**
 * zif_transaction_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.1.3
 **/
GQuark
zif_transaction_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_transaction_error");
	return quark;
}

/**
 * zif_transaction_reason_to_string:
 * @reason: the #ZifTransactionReason enum, e.g. %ZIF_TRANSACTION_REASON_INSTALL_USER_ACTION
 *
 * Gets the string representation of the reason a package was added.
 *
 * Return value: A constant string
 *
 * Since: 0.1.3
 **/
const gchar *
zif_transaction_reason_to_string (ZifTransactionReason reason)
{
	if (reason == ZIF_TRANSACTION_REASON_INSTALL_USER_ACTION)
		return "install-user-action";
	if (reason == ZIF_TRANSACTION_REASON_REMOVE_USER_ACTION)
		return "remove-user-action";
	if (reason == ZIF_TRANSACTION_REASON_UPDATE_USER_ACTION)
		return "update-user-action";
	if (reason == ZIF_TRANSACTION_REASON_REMOVE_AS_ONLYN)
		return "remove-as-onlyn";
	if (reason == ZIF_TRANSACTION_REASON_INSTALL_DEPEND)
		return "install-depend";
	if (reason == ZIF_TRANSACTION_REASON_REMOVE_OBSOLETE)
		return "remove-obsolete";
	if (reason == ZIF_TRANSACTION_REASON_REMOVE_FOR_UPDATE)
		return "remove-for-update";
	if (reason == ZIF_TRANSACTION_REASON_INSTALL_FOR_UPDATE)
		return "install-for-update";
	if (reason == ZIF_TRANSACTION_REASON_REMOVE_FOR_DEP)
		return "remove-for-dep";
	g_warning ("cannot convert reason %i to string", reason);
	return NULL;
}

/**
 * zif_transaction_state_to_string:
 * @state: the #ZifTransactionState enum, e.g. %ZIF_TRANSACTION_STATE_RESOLVED
 *
 * Gets the string representation of the transaction state.
 *
 * Return value: A constant string
 *
 * Since: 0.1.3
 **/
const gchar *
zif_transaction_state_to_string (ZifTransactionState state)
{
	if (state == ZIF_TRANSACTION_STATE_CLEAN)
		return "clean";
	if (state == ZIF_TRANSACTION_STATE_RESOLVED)
		return "resolved";
	if (state == ZIF_TRANSACTION_STATE_PREPARED)
		return "prepared";
	if (state == ZIF_TRANSACTION_STATE_COMMITTED)
		return "committed";
	g_warning ("cannot convert state %i to string", state);
	return NULL;
}


/**
 * zif_transaction_get_package_array:
 **/
static GPtrArray *
zif_transaction_get_package_array (GPtrArray *array)
{
	GPtrArray *packages;
	ZifTransactionItem *item;
	guint i;

	/* just copy out the package data */
	packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->cancelled)
			continue;
		g_ptr_array_add (packages, g_object_ref (item->package));
	}
	return packages;
}

/**
 * zif_transaction_get_install:
 * @transaction: the #ZifTransaction object
 *
 * Gets the list of packages to be installed.
 *
 * Return value: A #GPtrArray of #ZifPackages, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_transaction_get_install (ZifTransaction *transaction)
{
	return zif_transaction_get_package_array (transaction->priv->install);
}

/**
 * zif_transaction_get_remove:
 * @transaction: the #ZifTransaction object
 *
 * Gets the list of packages to be removed.
 *
 * Return value: A #GPtrArray of #ZifPackages, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_transaction_get_remove (ZifTransaction *transaction)
{
	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), NULL);
	return zif_transaction_get_package_array (transaction->priv->remove);
}

/**
 * zif_transaction_item_free:
 **/
static void
zif_transaction_item_free (ZifTransactionItem *item)
{
	g_object_unref (item->package);
	g_ptr_array_unref (item->related_packages);
	g_free (item);
}

/**
 * zif_transaction_get_item_from_hash:
 **/
static ZifTransactionItem *
zif_transaction_get_item_from_hash (GHashTable *hash, ZifPackage *package)
{
	ZifTransactionItem *item;
	const gchar *package_id;

	/* find a package that matches */
	package_id = zif_package_get_id (package);
	item = g_hash_table_lookup (hash, package_id);
	return item;
}

/**
 * zif_transaction_get_item_from_array_by_related_package:
 **/
static ZifTransactionItem *
zif_transaction_get_item_from_array_by_related_package (GPtrArray *array,
							 ZifPackage *package)
{
	const gchar *package_id;
	guint i;
	guint j;
	ZifPackage *related_package;
	ZifTransactionItem *item;

	/* find a package that matches */
	package_id = zif_package_get_id (package);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->related_packages->len == 0)
			continue;
		for (j=0; j<item->related_packages->len; j++) {
			related_package = g_ptr_array_index (item->related_packages, j);
			if (g_strcmp0 (zif_package_get_id (related_package), package_id) == 0)
				return item;
		}
	}
	return NULL;
}

/**
 * zif_transaction_get_reason:
 * @transaction: the #ZifTransaction object
 * @package: the #ZifPackage object
 *
 * Gets the reason why the package is in the install or remove array.
 *
 * Return value: A #ZifTransactionReason enumerated value, or %ZIF_TRANSACTION_REASON_INVALID for error.
 *
 * Since: 0.1.3
 **/
ZifTransactionReason
zif_transaction_get_reason (ZifTransaction *transaction,
			    ZifPackage *package,
			    GError **error)
{
	ZifTransactionItem *item;
	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), ZIF_TRANSACTION_REASON_INVALID);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), ZIF_TRANSACTION_REASON_INVALID);
	g_return_val_if_fail (error == NULL || *error == NULL, ZIF_TRANSACTION_REASON_INVALID);

	/* find the package */
	item = zif_transaction_get_item_from_hash (transaction->priv->install_hash, package);
	if (item != NULL)
		return item->reason;
	item = zif_transaction_get_item_from_hash (transaction->priv->remove_hash, package);
	if (item != NULL)
		return item->reason;

	/* not found */
	g_set_error (error,
		     ZIF_TRANSACTION_ERROR,
		     ZIF_TRANSACTION_ERROR_FAILED,
		     "could not find package %s",
		     zif_package_get_id (package));
	return ZIF_TRANSACTION_REASON_INVALID;
}

/**
 * zif_transaction_get_array_for_reason:
 * @transaction: the #ZifTransaction object
 * @reason: the #ZifTransactionReason enum, e.g. %ZIF_TRANSACTION_REASON_INVALID
 *
 * Gets a list of packages that are due to be processed for a specific reason.
 *
 * Return value: An array of #ZifPackages, or %NULL for error. Free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_transaction_get_array_for_reason (ZifTransaction *transaction, ZifTransactionReason reason)
{
	GPtrArray *array;
	guint i;
	ZifTransactionItem *item;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), NULL);

	/* always create array, even for no results */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* find all the installed packages that match */
	for (i=0; i<transaction->priv->install->len; i++) {
		item = g_ptr_array_index (transaction->priv->install, i);
		if (item->cancelled)
			continue;
		if (item->reason == reason)
			g_ptr_array_add (array, g_object_ref (item->package));
	}

	/* find all the removed packages that match */
	for (i=0; i<transaction->priv->remove->len; i++) {
		item = g_ptr_array_index (transaction->priv->remove, i);
		if (item->cancelled)
			continue;
		if (item->reason == reason)
			g_ptr_array_add (array, g_object_ref (item->package));
	}
	return array;
}

/**
 * zif_transaction_add_to_array:
 **/
static gboolean
zif_transaction_add_to_array (GPtrArray *array,
			      GHashTable *hash,
			      ZifPackage *package,
			      GPtrArray *related_packages,
			      ZifTransactionReason reason)
{
	gboolean ret = FALSE;
	ZifTransactionItem *item;
	guint i;
	ZifPackage *package_tmp;

	/* already added? */
	item = zif_transaction_get_item_from_hash (hash, package);
	if (item != NULL)
		goto out;

	/* create new item */
	item = g_new0 (ZifTransactionItem, 1);
	item->resolved = FALSE;
	item->reason = reason;
	item->package = g_object_ref (package);
	item->related_packages = zif_package_array_new ();
	g_ptr_array_add (array, item);

	/* copy in related_packages, ignoring the package itself */
	if (related_packages != NULL) {
		for (i=0; i<related_packages->len; i++) {
			package_tmp = g_ptr_array_index (related_packages, i);
			if (zif_package_compare (package_tmp, package) == 0)
				continue;
			g_ptr_array_add (item->related_packages,
					 g_object_ref (package_tmp));
		}
	}

	/* add to hash table also for super-quick lookup */
	g_hash_table_insert (hash,
			     g_strdup (zif_package_get_id (package)),
			     item);

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_transaction_get_package_id_descriptions:
 **/
static gchar *
zif_transaction_get_package_id_descriptions (GPtrArray *array)
{
	GString *string;
	guint i;
	ZifPackage *package;

	/* nothing */
	if (array == NULL || array->len == 0)
		return g_strdup ("none");

	/* make string list */
	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_string_append_printf (string, "%s,",
					zif_package_get_id (package));
	}
	g_string_set_size (string, string->len - 1);
	return g_string_free (string, FALSE);
}

/**
 * zif_transaction_check_excludes:
 **/
static gboolean
zif_transaction_check_excludes (ZifTransaction *transaction,
				ZifPackage *package,
				GError **error)
{
	gboolean ret = TRUE;
	gchar **excludes = NULL;
	guint i;

	/* check excludes */
	excludes = zif_config_get_strv (transaction->priv->config,
					"excludes",
					NULL);
	if (excludes == NULL)
		goto out;
	for (i=0; excludes[i] != NULL; i++) {
		if (g_strcmp0 (excludes[i],
			       zif_package_get_name (package)) == 0) {
			ret = FALSE;
			g_set_error (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_FAILED,
				     "package %s is excluded",
				     zif_package_get_name (package));
			goto out;
		}
	}
out:
	g_strfreev (excludes);
	return ret;
}

/**
 * zif_transaction_add_install_internal:
 **/
static gboolean
zif_transaction_add_install_internal (ZifTransaction *transaction,
				      ZifPackage *package,
				      GPtrArray *related_packages,
				      ZifTransactionReason reason,
				      GError **error)
{
	gboolean ret;
	gchar *related_packages_str = NULL;

	/* check excludes */
	ret = zif_transaction_check_excludes (transaction, package, error);
	if (!ret)
		goto out;

	/* add to install */
	ret = zif_transaction_add_to_array (transaction->priv->install,
					    transaction->priv->install_hash,
					    package,
					    related_packages,
					    reason);
	if (!ret) {
		/* an already added install is not a failure condition */
		ret = TRUE;
		goto out;
	}

	/* print what we've added */
	related_packages_str = zif_transaction_get_package_id_descriptions (related_packages);
	g_debug ("Add INSTALL %s [%s] (with related packages %s)",
		 zif_package_get_id (package),
		 zif_transaction_reason_to_string (reason),
		 related_packages_str);
out:
	g_free (related_packages_str);
	return ret;
}

/**
 * zif_transaction_add_install:
 * @transaction: the #ZifTransaction object
 * @package: the #ZifPackage object to add
 * @error: a #GError which is used on failure, or %NULL
 *
 * Adds a package to be installed to the transaction.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.1.3
 **/
gboolean
zif_transaction_add_install (ZifTransaction *transaction,
			     ZifPackage *package,
			     GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* add to install */
	ret = zif_transaction_add_install_internal (transaction,
						    package,
						    NULL,
						    ZIF_TRANSACTION_REASON_INSTALL_USER_ACTION,
						    error);
	return ret;
}

/**
 * zif_transaction_add_update_internal:
 **/
static gboolean
zif_transaction_add_update_internal (ZifTransaction *transaction,
				     ZifPackage *package,
				     GPtrArray *related_packages,
				     ZifTransactionReason reason,
				     GError **error)
{
	gboolean ret;
	gchar *related_packages_str = NULL;

	/* check excludes */
	ret = zif_transaction_check_excludes (transaction, package, error);
	if (!ret)
		goto out;

	/* add to update */
	ret = zif_transaction_add_to_array (transaction->priv->update,
					    transaction->priv->update_hash,
					    package,
					    related_packages,
					    reason);
	if (!ret) {
		/* an already added update is not a failure condition */
		ret = TRUE;
		goto out;
	}

	/* print what we've added */
	related_packages_str = zif_transaction_get_package_id_descriptions (related_packages);
	g_debug ("Add UPDATE %s [%s] (with related packages %s)",
		 zif_package_get_id (package),
		 zif_transaction_reason_to_string (reason),
		 related_packages_str);
out:
	g_free (related_packages_str);
	return ret;
}

/**
 * zif_transaction_add_update:
 * @transaction: the #ZifTransaction object
 * @package: the #ZifPackage object to add
 * @error: a #GError which is used on failure, or %NULL
 *
 * Adds a package to be updated to the transaction.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.1.3
 **/
gboolean
zif_transaction_add_update (ZifTransaction *transaction, ZifPackage *package, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* add to update */
	ret = zif_transaction_add_update_internal (transaction,
						   package,
						   NULL,
						   ZIF_TRANSACTION_REASON_UPDATE_USER_ACTION,
						   error);
	return ret;
}

/**
 * zif_transaction_add_remove_internal:
 **/
static gboolean
zif_transaction_add_remove_internal (ZifTransaction *transaction,
				     ZifPackage *package,
				     GPtrArray *related_packages,
				     ZifTransactionReason reason,
				     GError **error)
{
	gboolean ret = FALSE;
	guint i;
	gchar **protected_packages = NULL;
	gchar *related_packages_str = NULL;

	/* is package protected */
	protected_packages = zif_config_get_strv (transaction->priv->config,
						  "protected_packages",
						  NULL);
	if (protected_packages != NULL) {
		for (i=0; protected_packages[i] != NULL; i++) {
			if (g_strcmp0 (protected_packages[i],
				       zif_package_get_name (package)) == 0) {
				g_set_error (error,
					     ZIF_TRANSACTION_ERROR,
					     ZIF_TRANSACTION_ERROR_FAILED,
					     "cannot remove protected package %s",
					     zif_package_get_name (package));
				goto out;
			}
		}
	}

	/* check excludes */
	ret = zif_transaction_check_excludes (transaction, package, error);
	if (!ret)
		goto out;

	/* add to remove */
	ret = zif_transaction_add_to_array (transaction->priv->remove,
					    transaction->priv->remove_hash,
					    package,
					    related_packages,
					    reason);
	if (!ret) {
		/* an already added remove is not a failure condition */
		ret = TRUE;
		goto out;
	}

	/* print what we've added */
	related_packages_str = zif_transaction_get_package_id_descriptions (related_packages);
	g_debug ("Add REMOVE %s [%s] (with related packages %s)",
		 zif_package_get_id (package),
		 zif_transaction_reason_to_string (reason),
		 related_packages_str);
out:
	g_free (related_packages_str);
	g_strfreev (protected_packages);
	return ret;
}

/**
 * zif_transaction_add_remove:
 * @transaction: the #ZifTransaction object
 * @package: the #ZifPackage object to add
 * @error: a #GError which is used on failure, or %NULL
 *
 * Adds a package to be removed to the transaction.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.1.3
 **/
gboolean
zif_transaction_add_remove (ZifTransaction *transaction, ZifPackage *package, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* add to remove */
	ret = zif_transaction_add_remove_internal (transaction,
						   package,
						   NULL,
						   ZIF_TRANSACTION_REASON_REMOVE_USER_ACTION,
						   error);
	return ret;
}

/**
 * zif_transaction_get_package_provide_from_array:
 **/
static gboolean
zif_transaction_get_package_provide_from_array (GPtrArray *array,
						ZifDepend *depend,
						ZifPackage **package,
						ZifState *state,
						GError **error)
{
	gboolean ret = TRUE;
	guint i;
	ZifTransactionItem *item;
	ZifDepend *satisfies = NULL;

	/* interate through the array */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* does this match */
		ret = zif_package_provides (item->package,
					    depend,
					    &satisfies,
					    state,
					    error);
		if (!ret) {
			g_assert (error == NULL || *error != NULL);
			goto out;
		}

		/* gotcha */
		if (satisfies != NULL) {
			*package = g_object_ref (item->package);
			g_object_unref (satisfies);
			goto out;
		}
	}

	/* success, but did not find */
	ret = TRUE;
	*package = NULL;
out:
	return ret;
}

/**
 * _zif_package_array_filter_best_provide:
 **/
static gboolean
_zif_package_array_filter_best_provide (ZifTransaction *transaction,
					GPtrArray *array,
					ZifDepend *depend,
					ZifPackage **package,
					ZifState *state,
					GError **error)
{
	gboolean exactarch;
	gboolean ret = TRUE;
	gchar *archinfo = NULL;
	ZifDepend *best_depend = NULL;
	GError *error_local = NULL;
	GPtrArray *depend_array = NULL;

	/* get the best depend for the results */
	ret = zif_package_array_provide (array, depend, &best_depend,
					 NULL, state, error);
	if (!ret)
		goto out;

	/* print what we've got */
	g_debug ("provide %s has %i matches",
		 zif_depend_get_description (depend),
		 array->len);
	if (best_depend != NULL) {
		g_debug ("best depend was %s",
			 zif_depend_get_description (best_depend));
	}

	/* is the exact arch required? */
	exactarch = zif_config_get_boolean (transaction->priv->config,
					    "exactarch", NULL);
	if (exactarch) {
		archinfo = zif_config_get_string (transaction->priv->config,
						  "archinfo", NULL);
		zif_package_array_filter_arch (array, archinfo);
	}

	/* filter these down so we get best architectures listed first */
	if (array->len > 1) {
		zif_package_array_filter_best_arch (array);
		g_debug ("after filtering by arch, array now %i packages", array->len);
	}

	/* if the depends are the same, choose the one with the biggest version */
	if (array->len > 1) {
		depend_array = zif_object_array_new ();
		zif_object_array_add (depend_array, best_depend);
		ret = zif_package_array_filter_provide (array,
							depend_array,
							state, error);
		if (!ret)
			goto out;
		g_debug ("after filtering by depend, array now %i packages", array->len);
	}

	/* filter these down so we get smallest names listed first */
	if (array->len > 1) {
		zif_package_array_filter_smallest_name (array);
		g_debug ("after filtering by name length, array now %i packages", array->len);
	}

	/* success, but no results */
	if (array->len == 0) {
		*package = NULL;
		goto out;
	}

	/* return the newest */
	*package = zif_package_array_get_newest (array, &error_local);
	if (*package == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get newest: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (archinfo);
	if (best_depend != NULL)
		g_object_unref (best_depend);
	if (depend_array != NULL)
		g_ptr_array_unref (depend_array);
	return ret;
}

/**
 * zif_transaction_get_package_provide_from_store:
 *
 * Returns a package that provides something.
 **/
static gboolean
zif_transaction_get_package_provide_from_store (ZifTransaction *transaction,
						ZifStore *store,
						ZifDepend *depend,
						ZifPackage **package,
						ZifState *state,
						GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *depend_array = NULL;
	GError *error_local = NULL;
	gboolean ret;
	ZifState *state_local;

	/* setup states */
	ret = zif_state_set_steps (state,
				   error,
				   80, /* search */
				   20, /* filter */
				   -1);
	if (!ret)
		goto out;

	/* add to array for searching */
	depend_array = zif_object_array_new ();
	zif_object_array_add (depend_array, depend);

	/* get provides */
	state_local = zif_state_get_child (state);
	array = zif_store_what_provides (store, depend_array, state, &error_local);
	if (array == NULL) {
		/* ignore this error */
		if (error_local->domain == ZIF_STORE_ERROR &&
		    error_local->code == ZIF_STORE_ERROR_ARRAY_IS_EMPTY) {
			g_error_free (error_local);
		} else {
			g_propagate_error (error, error_local);
			goto out;
		}
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* filter by best depend */
	if (array != NULL && array->len > 0) {

		/* get an array of packages that provide this */
		state_local = zif_state_get_child (state);
		ret = _zif_package_array_filter_best_provide (transaction, array, depend,
							      package, state_local, error);
		if (!ret)
			goto out;
	} else {
		*package = NULL;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	ret = TRUE;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (depend_array != NULL)
		g_ptr_array_unref (depend_array);
	return ret;

}

/**
 * zif_transaction_get_package_requires_from_store:
 *
 * Returns an array of packages that require something.
 **/
static gboolean
zif_transaction_get_package_requires_from_store (ZifStore *store,
						 ZifDepend *depend,
						 GHashTable *already_marked_to_remove,
						 GPtrArray **requires,
						 ZifState *state,
						 GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	GPtrArray *array;
	guint i;
	ZifPackage *package;
	ZifTransactionItem *item;
	ZifDepend *satisfies = NULL;

	/* get the package list */
	zif_state_reset (state);
	array = zif_store_get_packages (store, state, &error_local);
	if (array == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get installed package list: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* search it */
	*requires = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);

		/* is already being removed? */
		item = zif_transaction_get_item_from_hash (already_marked_to_remove,
							   package);
		if (item != NULL) {
			if (0) {
				g_debug ("not getting requires for %s, as already in remove array",
					 zif_package_get_id (package));
			}
			continue;
		}

		/* get requires */
		ret = zif_package_requires (package,
					    depend,
					    &satisfies,
					    state,
					    error);
		if (!ret) {
			g_assert (error == NULL || *error != NULL);
			goto out;
		}

		/* gotcha */
		if (satisfies != NULL) {
			g_debug ("adding %s to requires", zif_package_get_id (package));
			g_ptr_array_add (*requires, g_object_ref (package));
			g_object_unref (satisfies);
		}
	}

	/* success */
	ret = TRUE;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;

}

/**
 * zif_transaction_get_packages_provides_from_store_array:
 *
 * Returns an array of packages that provide something.
 **/
static gboolean
zif_transaction_get_packages_provides_from_store_array (ZifTransaction *transaction,
							GPtrArray *store_array,
							ZifDepend *depend,
							GPtrArray **array,
							ZifState *state,
							GError **error)
{
	guint i;
	ZifPackage *package = NULL;
	ZifStore *store;
	gboolean ret = TRUE;

	/* find the depend in the store array */
	*array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<store_array->len; i++) {
		store = g_ptr_array_index (store_array, i);

		/* get provide */
		ret = zif_transaction_get_package_provide_from_store (transaction,
								      store,
								      depend,
								      &package,
								      state,
								      error);
		if (!ret) {
			g_assert (error == NULL || *error != NULL);
			goto out;
		}

		/* gotcha */
		if (package != NULL)
			g_ptr_array_add (*array, package);
	}

	/* success */
	ret = TRUE;
out:
	return ret;

}

/**
 * zif_transaction_get_package_provide_from_store_array:
 **/
static gboolean
zif_transaction_get_package_provide_from_store_array (ZifTransaction *transaction,
						      GPtrArray *store_array,
						      ZifDepend *depend,
						      ZifPackage **package,
						      ZifState *state,
						      GError **error)
{
	GPtrArray *array = NULL;
	gboolean ret;
	GError *error_local = NULL;

	/* get the list */
	ret = zif_transaction_get_packages_provides_from_store_array (transaction,
								      store_array,
								      depend,
								      &array,
								      state,
								      error);
	if (!ret)
		goto out;

	/* success, but found nothing */
	g_debug ("found %i provides for %s",
		 array->len,
		 zif_depend_get_description (depend));
	if (array->len == 0) {
		*package = NULL;
		goto out;
	}

	/* return the newest package that provides the dep */
	*package = zif_package_array_get_newest (array, &error_local);
	if (*package == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Failed to filter newest: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_transaction_resolve_install_depend:
 **/
static gboolean
zif_transaction_resolve_install_depend (ZifTransactionResolve *data,
					ZifDepend *depend,
					ZifPackage *reason,
					GError **error)
{
	gboolean ret = TRUE;
	ZifPackage *package_provide = NULL;
	GPtrArray *already_installed = NULL;
	GPtrArray *related_packages = NULL;
	GError *error_local = NULL;
	const gchar *to_array[] = {NULL, NULL};
	ZifPackage *package;
	guint i;

	/* already provided by something in the install set */
	g_debug ("searching in install");
	ret = zif_transaction_get_package_provide_from_array (data->transaction->priv->install,
							      depend, &package_provide,
							      data->state, error);
	if (!ret) {
		g_assert (error == NULL || *error != NULL);
		goto out;
	}
	if (package_provide != NULL) {
		g_debug ("depend %s is already provided by %s (to be installed)",
			 zif_depend_get_description (depend),
			 zif_package_get_id (package_provide));
		goto out;
	}

	/* already provided in the rpmdb */
	g_debug ("searching for %s in local",
		 zif_depend_get_description (depend));
	ret = zif_transaction_get_package_provide_from_store (data->transaction,
							      data->transaction->priv->store_local,
							      depend, &package_provide,
							      data->state, error);
	if (!ret) {
		g_assert (error == NULL || *error != NULL);
		goto out;
	}
	if (package_provide != NULL) {
		g_debug ("depend %s is already provided by %s (installed)",
			 zif_depend_get_description (depend),
			 zif_package_get_id (package_provide));
		goto out;
	}

	/* provided by something to be installed */
	g_debug ("searching in remote");
	ret = zif_transaction_get_package_provide_from_store_array (data->transaction,
								    data->transaction->priv->stores_remote,
								    depend, &package_provide,
								    data->state, error);
	if (!ret) {
		g_assert (error == NULL || *error != NULL);
		goto out;
	}

	/* make a list of all the packages to revert if this item fails */
	related_packages = zif_package_array_new ();
	g_ptr_array_add (related_packages, g_object_ref (reason));

	if (package_provide != NULL) {
		g_debug ("depend %s is provided by %s (available)",
			 zif_depend_get_description (depend),
			 zif_package_get_id (package_provide));

		/* is this updating an existing package */
		to_array[0] = zif_package_get_name (package_provide);
		zif_state_reset (data->state);
		already_installed = zif_store_resolve (data->transaction->priv->store_local,
						       (gchar**)to_array,
						       data->state,
						       &error_local);
		if (already_installed == NULL) {
			/* this is special */
			if (error_local->domain == ZIF_STORE_ERROR ||
			    error_local->code == ZIF_STORE_ERROR_ARRAY_IS_EMPTY) {
				g_error_free (error_local);
				goto skip_resolve;
			}
			ret = FALSE;
			g_set_error (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_FAILED,
				     "Failed to resolve local: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* add this */
		g_ptr_array_add (related_packages, g_object_ref (package_provide));

		/* remove old versions */
		for (i=0; i<already_installed->len; i++) {
			package = g_ptr_array_index (already_installed, i);
			g_debug ("%s is already installed, and we want %s, so removing installed version",
				 zif_package_get_id (package),
				 zif_package_get_id (package_provide));

			/* add this */
			g_ptr_array_add (related_packages, g_object_ref (package));
			ret = zif_transaction_add_remove_internal (data->transaction,
								   package,
								   related_packages,
								   ZIF_TRANSACTION_REASON_REMOVE_FOR_UPDATE,
								   error);
			if (!ret)
				goto out;

			/* remove from the planned local store */
			zif_array_remove (data->post_resolve_package_array, package_provide);
		}
skip_resolve:

		/* add the provide to the install set */
		ret = zif_transaction_add_install_internal (data->transaction,
							    package_provide,
							    related_packages,
							    ZIF_TRANSACTION_REASON_INSTALL_DEPEND,
							    error);
		if (!ret)
			goto out;

		/* add to the planned local store */
		zif_array_add (data->post_resolve_package_array, package_provide);

		data->unresolved_dependencies = TRUE;
		goto out;
	}

	/* failed */
	ret = FALSE;
	g_set_error (error,
		     ZIF_TRANSACTION_ERROR,
		     ZIF_TRANSACTION_ERROR_FAILED,
		     "nothing provides %s which is required by %s",
		     zif_depend_get_description (depend),
		     zif_package_get_id (reason));
out:
	if (already_installed != NULL)
		g_ptr_array_unref (already_installed);
	if (related_packages != NULL)
		g_ptr_array_unref (related_packages);
	if (package_provide != NULL)
		g_object_unref (package_provide);
	return ret;
}

/**
 * zif_transaction_resolve_install_item:
 **/
static gboolean
zif_transaction_resolve_install_item (ZifTransactionResolve *data,
				      ZifTransactionItem *item,
				      GError **error)
{
	const gchar *to_array[] = {NULL, NULL};
	gboolean ret = FALSE;
	GError *error_local = NULL;
	GPtrArray *requires = NULL;
	GPtrArray *related_packages = NULL;
	guint i;
	ZifDepend *depend;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifPackage *package_oldest = NULL;
	ZifTransactionItem *item_tmp;
	guint installonlyn = 1;
	gchar **installonlypkgs = NULL;
	ZifTransactionPrivate *priv = data->transaction->priv;

	/* is already installed and we are not already removing it */
	to_array[0] = zif_package_get_name (item->package);
	zif_state_reset (data->state);
	array = zif_store_resolve (data->transaction->priv->store_local,
				   (gchar**)to_array,
				   data->state, &error_local);
	if (array == NULL) {
		/* this is special */
		if (error_local->domain == ZIF_STORE_ERROR &&
		    error_local->code == ZIF_STORE_ERROR_ARRAY_IS_EMPTY) {
			g_clear_error (&error_local);
			goto skip_resolve;
		}
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Failed to resolve local: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* some packages are special */
	installonlypkgs = zif_config_get_strv (priv->config, "installonlypkgs", error);
	if (installonlypkgs == NULL) {
		ret = FALSE;
		goto out;
	}
	for (i=0; installonlypkgs[i] != NULL; i++) {
		if (g_strcmp0 (zif_package_get_name (item->package),
			       installonlypkgs[i]) == 0) {
			installonlyn = zif_config_get_uint (priv->config,
							    "installonly_limit",
							    NULL);
			break;
		}
	}

	/* make a list of all the packages to revert if this item fails */
	related_packages = zif_package_array_new ();
	g_ptr_array_add (related_packages, g_object_ref (item->package));

	/* have we got more that that installed? */
	if (array->len >= installonlyn) {

		/* is it the same package? */
		package = g_ptr_array_index (array, 0);
		if (zif_package_compare (package, item->package) == 0) {
			ret = FALSE;
			g_set_error (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_NOTHING_TO_DO,
				     "the package %s is already installed",
				     zif_package_get_id (package));
			goto out;
		}

		/* need to remove the oldest one */
		package_oldest = zif_package_array_get_oldest (array, &error_local);
		if (package_oldest == NULL) {
			ret = FALSE;
			g_set_error (error, ZIF_TRANSACTION_ERROR, ZIF_TRANSACTION_ERROR_FAILED,
				     "failed to get oldest for package array: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* remove it, if it has not been removed already */
		item_tmp = zif_transaction_get_item_from_hash (data->transaction->priv->remove_hash,
							       package_oldest);
		if (item_tmp == NULL) {
			g_debug ("installing package %s would have %i versions installed (maximum %i) so removing %s",
				 zif_package_get_id (item->package),
				 array->len,
				 installonlyn,
				 zif_package_get_id (package_oldest));
			ret = zif_transaction_add_remove_internal (data->transaction,
								   package_oldest,
								   related_packages,
								   ZIF_TRANSACTION_REASON_REMOVE_AS_ONLYN,
								   error);
			if (!ret) {
				g_assert (error == NULL || *error != NULL);
				goto out;
			}

			/* remove from the planned local store */
			zif_array_remove (data->post_resolve_package_array, package_oldest);
		}
	}

skip_resolve:

	g_debug ("getting requires for %s", zif_package_get_id (item->package));
	zif_state_reset (data->state);
	requires = zif_package_get_requires (item->package, data->state, &error_local);
	if (requires == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get requires for %s: %s",
			     zif_package_get_id (item->package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
	g_debug ("got %i requires", requires->len);

	/* find each require */
	for (i=0; i<requires->len; i++) {
		depend = g_ptr_array_index (requires, i);
		ret = zif_transaction_resolve_install_depend (data, depend, item->package, error);
		if (!ret) {
			g_assert (error == NULL || *error != NULL);
			goto out;
		}
	}

	/* item is good now all the requires exist in the set */
	item->resolved = TRUE;
	ret = TRUE;
out:
	if (!ret) {
		g_assert (error == NULL || *error != NULL);
	}
	g_strfreev (installonlypkgs);
	if (related_packages != NULL)
		g_ptr_array_unref (related_packages);
	if (package_oldest != NULL)
		g_object_unref (package_oldest);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (requires != NULL)
		g_ptr_array_unref (requires);
	return ret;
}

/**
 * zif_transaction_resolve_remove_require:
 *
 * Remove any package that needs the @depend provided by @source.
 **/
static gboolean
zif_transaction_resolve_remove_require (ZifTransactionResolve *data,
					ZifTransactionItem *item,
					ZifDepend *depend,
					GError **error)
{
	gboolean ret = TRUE;
	guint i;
	GPtrArray *depend_array = NULL;
	GPtrArray *package_requires = NULL;
	GPtrArray *related_packages = NULL;
	ZifPackage *package;
	ZifPackage *package_in_install;
	ZifDepend *satisfies;
	GError *error_local = NULL;
	GPtrArray *local_provides = NULL;

	/* does anything *else* provide the depend that's installed? */
	if (data->transaction->priv->verbose) {
		g_debug ("find anything installed that also provides %s (currently provided by %s)",
			 zif_depend_get_description (depend),
			 zif_package_get_id (item->package));
	}
	zif_state_reset (data->state);
	depend_array = zif_object_array_new ();
	zif_object_array_add (depend_array, depend);
	local_provides = zif_store_what_provides (data->transaction->priv->store_local,
						  depend_array,
						  data->state,
						  &error_local);
	if (local_provides == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Failed to get local provide for %s: %s",
			     zif_depend_get_description (depend),
			     error_local->message);
		goto out;
	}

	/* find out if anything arch-compatible (that isn't the package itself)
	 * provides the dep */
	for (i=0; i<local_provides->len; i++) {
		package = g_ptr_array_index (local_provides, i);
		if (zif_package_compare (package, item->package) == 0)
			continue;
		if (!zif_package_is_compatible_arch (package, item->package))
			continue;
		g_debug ("got local provide from %s, so no need to remove",
			 zif_package_get_id (package));
		goto out;
	}

	/* find if anything in the local store requires this package */
	if (data->transaction->priv->verbose) {
		g_debug ("find anything installed that requires %s provided by %s",
			 zif_depend_get_description (depend),
			 zif_package_get_id (item->package));
	}
	ret = zif_transaction_get_package_requires_from_store (data->transaction->priv->store_local,
							       depend,
							       data->transaction->priv->remove_hash,
							       &package_requires,
							       data->state, error);
	if (!ret)
		goto out;
	if (package_requires == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "nothing installed requires %s",
			     zif_depend_get_description (depend));
		goto out;
	}

	/* make a list of all the packages to revert if this item fails */
	related_packages = zif_package_array_new ();
	g_ptr_array_add (related_packages, g_object_ref (item->package));

	/* print */
	if (data->transaction->priv->verbose) {
		g_debug ("%i packages require %s provided by %s",
			 package_requires->len,
			 zif_depend_get_description (depend),
			 zif_package_get_id (item->package));
		for (i=0; i<package_requires->len; i++) {
			package = g_ptr_array_index (package_requires, i);
			g_debug ("%i.\t%s", i+1, zif_package_get_id (package));
		}
	}
	for (i=0; i<package_requires->len; i++) {
		package = g_ptr_array_index (package_requires, i);

		/* don't remove ourself */
		if (item->package == package)
			continue;

		/* is the thing that the package requires provided by something in install
		 * NOTE: we need to get the actual depend of the package, not the thing passed to us */
		ret = zif_package_requires (package, depend, &satisfies, data->state, error);
		if (!ret)
			goto out;

		/* this should be true, otherwise it would not have been added to the array */
		g_assert (satisfies != NULL);

		/* find out if anything in the install queue already provides the depend */
		if (data->transaction->priv->verbose) {
			g_debug ("find out if %s is provided in the install queue",
				 zif_depend_get_description (satisfies));
		}
		ret = zif_transaction_get_package_provide_from_array (data->transaction->priv->install,
								      satisfies,
								      &package_in_install,
								      data->state,
								      error);
		if (!ret)
			goto out;
		if (package_in_install != NULL) {
			g_debug ("%s provides %s which is already being installed",
				 zif_package_get_id (package_in_install),
				 zif_depend_get_description (depend));
			g_object_unref (package_in_install);
			continue;
		}

		g_object_unref (satisfies);

		/* remove this too */
		g_debug ("depend %s is required by %s (installed), so remove",
			 zif_depend_get_description (depend),
			 zif_package_get_id (package));

		/* add this item too */
		g_ptr_array_add (related_packages, g_object_ref (package));

		/* package is being updated, so try to update deps too */
		if (item->reason == ZIF_TRANSACTION_REASON_REMOVE_FOR_UPDATE) {
			ret = zif_transaction_add_update_internal (data->transaction,
								   package,
								   related_packages,
								   item->reason,
								   error);
			if (!ret)
				goto out;

		} else {
			/* remove the package */
			ret = zif_transaction_add_remove_internal (data->transaction,
								   package,
								   related_packages,
								   ZIF_TRANSACTION_REASON_REMOVE_FOR_DEP,
								   error);
			if (!ret)
				goto out;

			/* remove from the planned local store */
			zif_array_remove (data->post_resolve_package_array, package);
		}
		data->unresolved_dependencies = TRUE;
	}
out:
	if (related_packages != NULL)
		g_ptr_array_unref (related_packages);
	if (depend_array != NULL)
		g_ptr_array_unref (depend_array);
	if (local_provides != NULL)
		g_ptr_array_unref (local_provides);
	if (package_requires != NULL)
		g_ptr_array_unref (package_requires);
	return ret;
}

/**
 * zif_transaction_resolve_remove_item:
 **/
static gboolean
zif_transaction_resolve_remove_item (ZifTransactionResolve *data,
				     ZifTransactionItem *item,
				     GError **error)
{
	GPtrArray *provides = NULL;
	guint i;
	GError *error_local = NULL;
	gboolean ret = FALSE;
	ZifDepend *depend;

	/* make a list of anything this package provides */
	g_debug ("getting provides for %s", zif_package_get_id (item->package));
	zif_state_reset (data->state);
	provides = zif_package_get_provides (item->package, data->state, &error_local);
	if (provides == NULL) {
		g_set_error (error, ZIF_TRANSACTION_ERROR, ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get provides for %s: %s",
			     zif_package_get_id (item->package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* find each provide */
	if (data->transaction->priv->verbose) {
		g_debug ("got %i provides", provides->len);
		for (i=0; i<provides->len; i++) {
			depend = g_ptr_array_index (provides, i);
			g_debug ("%i.\t%s", i+1, zif_depend_get_description (depend));
		}
	}
	for (i=0; i<provides->len; i++) {
		depend = g_ptr_array_index (provides, i);
		ret = zif_transaction_resolve_remove_require (data, item, depend, error);
		if (!ret)
			goto out;
	}

	/* item is good now all the provides exist in the set */
	item->resolved = TRUE;
	ret = TRUE;
out:
	if (provides != NULL)
		g_ptr_array_unref (provides);
	return ret;
}

/**
 * zif_transaction_show_array:
 **/
static void
zif_transaction_show_array (const gchar *title, GPtrArray *array)
{
	guint i;
	ZifTransactionItem *item;

	/* nothing to print */
	if (array->len == 0)
		return;

	/* print list */
	g_debug ("%s", title);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->cancelled)
			continue;
		g_debug ("%i.\t%s [%s]",
			 i+1,
			 zif_package_get_id (item->package),
			 zif_transaction_reason_to_string (item->reason));
	}
}

/**
 * zif_transaction_get_newest_from_remote_by_names:
 **/
static ZifPackage *
zif_transaction_get_newest_from_remote_by_names (ZifTransactionResolve *data, ZifPackage *package, GError **error)
{
	GPtrArray *matches;
	ZifPackage *package_best = NULL;
	const gchar *search[] = { NULL, NULL };
	GError *error_local = NULL;
	guint i;

	/* get resolve in the array */
	search[0] = zif_package_get_name (package);
	zif_state_reset (data->state);
	matches = zif_store_array_resolve (data->transaction->priv->stores_remote,
					   (gchar **) search, data->state, &error_local);
	if (matches == NULL) {

		/* this is a special error */
		if (error_local->code == ZIF_STORE_ERROR_ARRAY_IS_EMPTY) {
			g_set_error (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_NOTHING_TO_DO,
				     "cannot find newest remote package %s as store is empty",
				     zif_package_get_name (package));
			g_error_free (error_local);
			goto out;
		}

		/* just push this along */
		g_propagate_error (error, error_local);
		goto out;
	}

	/* we found nothing */
	if (matches->len == 0) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "cannot find newest remote package %s",
			     zif_package_get_name (package));
		goto out;
	}

	/* common case */
	if (matches->len == 1) {
		package_best = g_object_ref (g_ptr_array_index (matches, 0));
		goto out;
	}

	/* more then one */
	g_debug ("multiple remote stores provide %s",
		 zif_package_get_name (package));
	for (i=0; i<matches->len; i++) {
		package_best = g_ptr_array_index (matches, i);
		g_debug ("%i.\t%s", i+1, zif_package_get_id (package_best));
	}

	/* filter out any architectures that don't satisfy */
	for (i=0; i<matches->len;) {
		package_best = g_ptr_array_index (matches, i);
		if (!zif_package_is_compatible_arch (package, package_best)) {
			g_ptr_array_remove_index_fast (matches, i);
			continue;
		}
		i++;
	}

	/* common case */
	if (matches->len == 1) {
		package_best = g_object_ref (g_ptr_array_index (matches, 0));
		goto out;
	}

	/* more then one */
	g_debug ("multiple remote stores still provide %s",
		 zif_package_get_name (package));
	for (i=0; i<matches->len; i++) {
		package_best = g_ptr_array_index (matches, i);
		g_debug ("%i.\t%s", i+1, zif_package_get_id (package_best));
	}

	/* get the newest package */
	package_best = zif_package_array_get_newest (matches, error);
out:
	if (matches != NULL)
		g_ptr_array_unref (matches);
	return package_best;
}

/**
 * zif_transaction_resolve_update_item:
 **/
static gboolean
zif_transaction_resolve_update_item (ZifTransactionResolve *data,
				     ZifTransactionItem *item,
				     GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gint value;
	GPtrArray *obsoletes = NULL;
	GPtrArray *depend_array = NULL;
	GPtrArray *related_packages = NULL;
	guint i;
	ZifDepend *depend;
	ZifPackage *package = NULL;

	/* does anything obsolete this package */
	depend = zif_depend_new ();
	zif_depend_set_name (depend, zif_package_get_name (item->package));
	zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (depend, zif_package_get_version (item->package));

	/* make a list of all the packages to revert if this item fails */
	related_packages = zif_package_array_new ();
	g_ptr_array_add (related_packages, g_object_ref (item->package));

	/* search the remote stores */
	zif_state_reset (data->state);
	depend_array = zif_object_array_new ();
	zif_object_array_add (depend_array, depend);
	obsoletes = zif_store_array_what_obsoletes (data->transaction->priv->stores_remote,
						    depend_array,
						    data->state,
						    &error_local);
	if (obsoletes == NULL) {
		/* this is a special error */
		if (error_local->code == ZIF_STORE_ERROR_ARRAY_IS_EMPTY) {
			g_clear_error (&error_local);
			goto skip;
		}
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to find %s in remote store: %s",
			     zif_package_get_id (item->package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
	g_debug ("%i packages obsolete %s with %s",
		 obsoletes->len,
		 zif_package_get_id (item->package),
		 zif_depend_get_description (depend));
	if (obsoletes->len > 0) {
		for (i=0; i<obsoletes->len; i++) {
			package = g_ptr_array_index (obsoletes, i);
			g_debug ("%i.\t%s",
				 i+1,
				 zif_package_get_id (package));
		}

		/* get the newest package */
		package = zif_package_array_get_newest (obsoletes, error);
		if (package == NULL) {
			ret = FALSE;
			goto out;
		}

		/* remove the installed package */
		ret = zif_transaction_add_remove_internal (data->transaction,
							   item->package,
							   related_packages,
							   ZIF_TRANSACTION_REASON_REMOVE_OBSOLETE,
							   error);
		if (!ret)
			goto out;

		/* remove from the planned local store */
		zif_array_remove (data->post_resolve_package_array, item->package);

		/* is already installed */
		if (zif_transaction_get_item_from_hash (data->transaction->priv->install_hash,
							package) != NULL)
			goto success;

		/* add the new package */
		ret = zif_transaction_add_install_internal (data->transaction,
							    package,
							    related_packages,
							    item->reason,
							    error);
		if (!ret)
			goto out;

		/* add to the planned local store */
		zif_array_add (data->post_resolve_package_array, package);

		/* ignore all the other update checks */
		goto success;
	}

skip:

	/* get the newest package available from the remote stores */
	package = zif_transaction_get_newest_from_remote_by_names (data,
								   item->package,
								   &error_local);
	if (package == NULL) {
		/* this is a special error, just ignore the item */
		if (error_local->code == ZIF_TRANSACTION_ERROR_NOTHING_TO_DO) {
			g_error_free (error_local);
			item->resolved = TRUE;
			ret = TRUE;
			goto out;
		}
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to find %s in remote store: %s",
			     zif_package_get_id (item->package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* is the installed package the same? */
	value = zif_package_compare (package, item->package);
	if (value == 0) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_NOTHING_TO_DO,
			     "there is no update available for %s",
			     zif_package_get_id (package));
		goto out;
	}

	/* is the installed package newer */
	value = zif_package_compare (package, item->package);
	if (value <= 0) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_NOTHING_TO_DO,
			     "installed package %s is newer than package updated %s",
			     zif_package_get_id (item->package),
			     zif_package_get_id (package));
		goto out;
	}

	/* add this package */
	g_ptr_array_add (related_packages, g_object_ref (package));

	/* remove the installed package */
	ret = zif_transaction_add_remove_internal (data->transaction,
						   item->package,
						   related_packages,
						   ZIF_TRANSACTION_REASON_REMOVE_FOR_UPDATE,
						   error);
	if (!ret)
		goto out;

	/* remove from the planned local store */
	zif_array_remove (data->post_resolve_package_array, item->package);

	/* add the new package */
	ret = zif_transaction_add_install_internal (data->transaction,
						    package,
						    related_packages,
						    ZIF_TRANSACTION_REASON_INSTALL_FOR_UPDATE,
						    error);
	if (!ret)
		goto out;

	/* add to the planned local store */
	zif_array_add (data->post_resolve_package_array, package);

success:

	/* new things to process */
	data->unresolved_dependencies = TRUE;

	/* mark as resolved, so we don't try to process this again */
	item->resolved = TRUE;
out:
	if (depend_array != NULL)
		g_ptr_array_unref (depend_array);
	if (related_packages != NULL)
		g_ptr_array_unref (related_packages);
	if (obsoletes != NULL)
		g_ptr_array_unref (obsoletes);
	if (depend != NULL)
		g_object_unref (depend);
	if (package != NULL)
		g_object_unref (package);
	return ret;
}

/**
 * zif_transaction_get_package_conflict_from_package_array:
 **/
static gboolean
zif_transaction_get_package_conflict_from_package_array (GPtrArray *array,
							 ZifDepend *depend,
							 ZifPackage **package,
							 ZifState *state,
							 GError **error)
{
	gboolean ret = TRUE;
	GPtrArray *satisfy_array;
	GError *error_local = NULL;

	/* get an array of packages that provide this */
	ret = zif_package_array_conflict (array, depend, NULL,
					  &satisfy_array, state, error);
	if (!ret)
		goto out;

	/* print what we've got */
	g_debug ("conflict %s has %i matches",
		 zif_depend_get_description (depend),
		 satisfy_array->len);

	/* success, but no results */
	if (satisfy_array->len == 0) {
		*package = NULL;
		goto out;
	}

	/* return the newest */
	*package = zif_package_array_get_newest (satisfy_array, &error_local);
	if (*package == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get newest: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_ptr_array_unref (satisfy_array);
	return ret;
}

/**
 * zif_transaction_resolve_conflicts_item:
 **/
static gboolean
zif_transaction_resolve_conflicts_item (ZifTransactionResolve *data,
					ZifTransactionItem *item,
					GError **error)
{
	gboolean ret = TRUE;
	guint i;
	GPtrArray *provides = NULL;
	GPtrArray *conflicts = NULL;
	ZifPackage *conflicting;
	ZifDepend *depend;
	GPtrArray *results_tmp;
	GPtrArray *post_resolve_package_array = NULL;
	GError *error_local = NULL;

	/* get provides for the package */
	zif_state_reset (data->state);
	provides = zif_package_get_provides (item->package,
					     data->state,
					     &error_local);
	if (provides == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get provides: %s",
			     error_local->message);
		goto out;
	}

	/* get conflicts for the package */
	zif_state_reset (data->state);
	conflicts = zif_package_get_conflicts (item->package,
					       data->state,
					       &error_local);
	if (conflicts == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get conflicts: %s",
			     error_local->message);
		goto out;
	}

	/* get local base copy */
	post_resolve_package_array = zif_array_get_array (data->post_resolve_package_array);

	g_debug ("checking %i provides for %s",
		 provides->len,
		 zif_package_get_id (item->package));
	for (i=0; i<provides->len; i++) {

		depend = g_ptr_array_index (provides, i);
		g_debug ("checking provide %s",
			 zif_depend_get_description (depend));

		/* get packages that conflict with this */
		ret = zif_transaction_get_package_conflict_from_package_array (post_resolve_package_array,
									       depend, &conflicting,
									       data->state, error);
		if (!ret) {
			g_assert (error == NULL || *error != NULL);
			goto out;
		}

		/* something conflicts with the package */
		if (conflicting != NULL) {
			ret = FALSE;
			g_set_error (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_CONFLICTING,
				     "%s conflicted by %s",
				     zif_package_get_id (item->package),
				     zif_package_get_id (conflicting));
			g_object_unref (conflicting);
			goto out;
		}
	}

	g_debug ("checking %i conflicts for %s",
		 provides->len,
		 zif_package_get_id (item->package));
	for (i=0; i<conflicts->len; i++) {
		depend = g_ptr_array_index (conflicts, i);

		/* does this install conflict with another package */
		g_debug ("checking conflict %s",
			 zif_depend_get_description (depend));

		/* check if we conflict with something in the new
		 * installed array */
		ret = zif_package_array_provide (post_resolve_package_array,
						 depend,
						 NULL,
						 &results_tmp,
						 data->state,
						 error);
		if (!ret)
			goto out;

		/* we conflict with something */
		if (results_tmp->len > 0) {
			ret = FALSE;
			conflicting = zif_package_array_get_newest (results_tmp, NULL);
			g_set_error (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_CONFLICTING,
				     "%s conflicts with %s",
				     zif_package_get_id (item->package),
				     zif_package_get_id (conflicting));
			g_object_unref (conflicting);
			g_ptr_array_unref (results_tmp);
			goto out;
		}
		g_ptr_array_unref (results_tmp);
	}
out:
	if (post_resolve_package_array != NULL)
		g_ptr_array_unref (post_resolve_package_array);
	if (provides != NULL)
		g_ptr_array_unref (provides);
	if (conflicts != NULL)
		g_ptr_array_unref (conflicts);
	return ret;
}

/**
 * zif_transaction_resolve_wind_back_failure_package:
 **/
static void
zif_transaction_resolve_wind_back_failure_package (ZifTransaction *transaction, ZifPackage *package)
{
	ZifTransactionItem *item_tmp;

	g_debug ("winding back %s",
		 zif_package_get_id (package));

	/* remove the thing we just added to the install queue too */
	item_tmp = zif_transaction_get_item_from_hash (transaction->priv->install_hash,
						       package);
	if (item_tmp != NULL && !item_tmp->cancelled) {
		g_debug ("mark %s as CANCELLED",
			 zif_package_get_id (item_tmp->package));
		item_tmp->cancelled = TRUE;
	}
	item_tmp = zif_transaction_get_item_from_array_by_related_package (transaction->priv->install,
									   package);
	if (item_tmp != NULL && !item_tmp->cancelled) {
		g_debug ("mark %s as CANCELLED",
			 zif_package_get_id (item_tmp->package));
		item_tmp->cancelled = TRUE;
	}

	/* remove the thing we just added to remove queue too */
	item_tmp = zif_transaction_get_item_from_hash (transaction->priv->remove_hash,
						       package);
	if (item_tmp != NULL && !item_tmp->cancelled) {
		g_debug ("mark %s as CANCELLED",
			 zif_package_get_id (item_tmp->package));
		item_tmp->cancelled = TRUE;
	}
	item_tmp = zif_transaction_get_item_from_array_by_related_package (transaction->priv->remove,
									   package);
	if (item_tmp != NULL && !item_tmp->cancelled) {
		g_debug ("mark %s as CANCELLED",
			 zif_package_get_id (item_tmp->package));
		item_tmp->cancelled = TRUE;
	}
}

/**
 * zif_transaction_resolve_wind_back_failure:
 **/
static void
zif_transaction_resolve_wind_back_failure (ZifTransaction *transaction, ZifTransactionItem *item)
{
	guint i;
	ZifPackage *update_package;

	/* don't try to run this again */
	g_debug ("mark %s as CANCELLED",
		 zif_package_get_id (item->package));
	item->cancelled = TRUE;

	/* remove the things we just added to the install queue too */
	zif_transaction_resolve_wind_back_failure_package (transaction,
							   item->package);
	for (i=0; i<item->related_packages->len; i++) {
		update_package = g_ptr_array_index (item->related_packages, i);
		zif_transaction_resolve_wind_back_failure_package (transaction,
								   update_package);
	}
}

/**
 * zif_transaction_get_array_resolved:
 **/
static guint
zif_transaction_get_array_resolved (GPtrArray *array)
{
	guint i;
	guint resolved_items = 0;
	ZifTransactionItem *item;

	/* count each transaction that's been processed or ignored */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->resolved ||
		    item->cancelled) {
			resolved_items++;
			continue;
		}
	}
	return resolved_items;
}

/**
 * zif_transaction_set_progress:
 **/
static void
zif_transaction_set_progress (ZifTransaction *transaction, ZifState *state)
{
	guint max_items;
	guint percentage;
	guint resolved_items;

	/* update implies install *and* remove */
	max_items = transaction->priv->install->len +
		    (2 * transaction->priv->update->len)+
		    transaction->priv->remove->len;

	/* calculate how many we've already processed */
	resolved_items = zif_transaction_get_array_resolved (transaction->priv->install);
	resolved_items += zif_transaction_get_array_resolved (transaction->priv->remove);
	resolved_items += 2 * zif_transaction_get_array_resolved (transaction->priv->update);

	/* calculate using a rough metric */
	percentage = resolved_items * 100 / max_items;
	g_debug ("progress is %i/%i (%i%%)",
		 resolved_items, max_items, percentage);

	/* only set if the percentage is going to go up */
	if (zif_state_get_percentage (state) < percentage)
		zif_state_set_percentage (state, percentage);
}

/**
 * zif_transaction_get_array_success:
 **/
static guint
zif_transaction_get_array_success (GPtrArray *array)
{
	guint i;
	guint success = 0;
	ZifTransactionItem *item;

	/* count each transaction that's been processed or ignored */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->resolved)
			success++;
	}
	return success;
}

/**
 * zif_transaction_item_sort_cb:
 **/
static gint
zif_transaction_item_sort_cb (ZifTransactionItem **a, ZifTransactionItem **b)
{
	return g_strcmp0 (zif_package_get_name ((*a)->package),
			  zif_package_get_name ((*b)->package));
}

/**
 * zif_transaction_setup_post_resolve_package_array:
 *
 * We track the installed post resolve state to make conflicts checking
 * much quicker. We don't have to search entries that are already removed
 * and can do saner conflicts handling.
 **/
static gboolean
zif_transaction_setup_post_resolve_package_array (ZifTransactionResolve *data,
						  GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *packages = NULL;
	guint i;
	ZifPackage *package_tmp;
	ZifTransactionItem *item;
	ZifTransactionPrivate *priv = data->transaction->priv;

	/* add existing installed packages */
	packages = zif_store_get_packages (priv->store_local,
					   data->state, error);
	if (packages == NULL)
		goto out;
	for (i=0; i<packages->len; i++) {
		package_tmp = g_ptr_array_index (packages, i);
		zif_array_add (data->post_resolve_package_array, package_tmp);
	}

	/* coldplug */
	for (i=0; i<priv->install->len; i++) {
		item = g_ptr_array_index (priv->install, i);
		zif_array_add (data->post_resolve_package_array, item->package);
	}
	for (i=0; i<priv->remove->len; i++) {
		item = g_ptr_array_index (priv->remove, i);
		zif_array_remove (data->post_resolve_package_array, item->package);
	}

	/* success */
	ret = TRUE;
	g_debug ("%i already in world state", data->post_resolve_package_array->len);
out:
	if (packages != NULL)
		g_ptr_array_unref (packages);
	return ret;
}

/**
 * zif_transaction_resolve:
 * @transaction: the #ZifTransaction object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Resolves the transaction ensuring all dependancies are met.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.1.3
 **/
gboolean
zif_transaction_resolve (ZifTransaction *transaction, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	gboolean skip_broken;
	GError *error_local = NULL;
	guint i;
	guint items_success;
	guint resolve_count = 0;
	ZifTransactionItem *item;
	ZifTransactionPrivate *priv;
	ZifTransactionResolve *data = NULL;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (transaction->priv->stores_remote != NULL, FALSE);
	g_return_val_if_fail (transaction->priv->store_local != NULL, FALSE);

	/* get private */
	priv = transaction->priv;

	g_debug ("starting resolve with %i to install, %i to update, and %i to remove",
		 priv->install->len,
		 priv->update->len,
		 priv->remove->len);

	/* get defaults */
	skip_broken = zif_config_get_boolean (priv->config,
					      "skip_broken",
					      NULL);

	/* whilst there are unresolved dependencies, keep trying */
	data = g_new0 (ZifTransactionResolve, 1);
	zif_state_set_number_steps (state, 1);
	data->state = zif_state_get_child (state);
	data->post_resolve_package_array = zif_array_new ();
	zif_array_set_mapping_func (data->post_resolve_package_array,
				    (ZifArrayMappingFuncCb) zif_package_get_id);
	/* we can't do child progress in a sane way */
	zif_state_set_report_progress (data->state, FALSE);
	data->transaction = transaction;
	data->unresolved_dependencies = TRUE;

	/* create a new world view of the package database */
	ret = zif_transaction_setup_post_resolve_package_array (data, error);
	if (!ret)
		goto out;

	/* loop until all resolved */
	while (data->unresolved_dependencies) {

		/* reset here */
		resolve_count++;
		data->unresolved_dependencies = FALSE;

		/* for each package set to be installed */
		g_debug ("starting INSTALL on loop %i", resolve_count);
		for (i=0; i<priv->install->len; i++) {
			item = g_ptr_array_index (priv->install, i);
			if (item->resolved)
				continue;
			if (item->cancelled)
				continue;

			/* set action */
			zif_state_action_start (state,
						ZIF_STATE_ACTION_DEPSOLVING_INSTALL,
						zif_package_get_id (item->package));

			/* resolve this item */
			ret = zif_transaction_resolve_install_item (data, item, &error_local);
			if (!ret) {
				g_assert (error_local != NULL);
				/* special error code */
				if (error_local->code == ZIF_TRANSACTION_ERROR_NOTHING_TO_DO) {
					g_debug ("REMOVE %s as nothing to do",
						 zif_package_get_id (item->package));
					g_ptr_array_remove (priv->install, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				if (skip_broken) {
					g_debug ("ignoring error as we're skip-broken: %s",
						 error_local->message);
					zif_transaction_resolve_wind_back_failure (transaction, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				g_propagate_error (error, error_local);
				goto out;
			}

			/* set the approximate progress if possible */
			zif_transaction_set_progress (transaction, state);
		}

		/* for each package set to be updated */
		g_debug ("starting UPDATE on loop %i", resolve_count);
		for (i=0; i<priv->update->len; i++) {
			item = g_ptr_array_index (priv->update, i);
			if (item->resolved)
				continue;
			if (item->cancelled)
				continue;

			/* set action */
			zif_state_action_start (state,
						ZIF_STATE_ACTION_DEPSOLVING_UPDATE,
						zif_package_get_id (item->package));

			/* resolve this item */
			ret = zif_transaction_resolve_update_item (data, item, &error_local);
			if (!ret) {
				g_assert (error_local != NULL);
				/* special error code */
				if (error_local->code == ZIF_TRANSACTION_ERROR_NOTHING_TO_DO) {
					g_debug ("REMOVE %s as nothing to do",
						 zif_package_get_id (item->package));
					g_ptr_array_remove (priv->update, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				if (skip_broken) {
					g_debug ("ignoring error as we're skip-broken: %s",
						 error_local->message);
					zif_transaction_resolve_wind_back_failure (transaction, item);
					g_ptr_array_remove (priv->update, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				g_propagate_error (error, error_local);
				goto out;
			}

			/* set the approximate progress if possible */
			zif_transaction_set_progress (transaction, state);
		}

		/* for each package set to be removed */
		g_debug ("starting REMOVE on loop %i", resolve_count);
		for (i=0; i<priv->remove->len; i++) {
			item = g_ptr_array_index (priv->remove, i);
			if (item->resolved)
				continue;
			if (item->cancelled)
				continue;

			/* set action */
			zif_state_action_start (state,
						ZIF_STATE_ACTION_DEPSOLVING_REMOVE,
						zif_package_get_id (item->package));

			/* resolve this item */
			ret = zif_transaction_resolve_remove_item (data, item, &error_local);
			if (!ret) {
				g_assert (error_local != NULL);
				/* special error code */
				if (error_local->code == ZIF_TRANSACTION_ERROR_NOTHING_TO_DO) {
					g_debug ("REMOVE %s as nothing to do",
						 zif_package_get_id (item->package));
					g_ptr_array_remove (priv->remove, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				if (skip_broken) {
					g_debug ("ignoring error as we're skip-broken: %s",
						 error_local->message);
					zif_transaction_resolve_wind_back_failure (transaction, item);
					g_ptr_array_remove (priv->remove, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				g_propagate_error (error, error_local);
				goto out;
			}

			/* set the approximate progress if possible */
			zif_transaction_set_progress (transaction, state);
		}

		/* check conflicts */
		g_debug ("starting CONFLICTS on loop %i", resolve_count);
		for (i=0; i<priv->install->len; i++) {
			item = g_ptr_array_index (priv->install, i);
			if (item->cancelled)
				continue;

			/* set action */
			zif_state_action_start (state,
						ZIF_STATE_ACTION_DEPSOLVING_CONFLICTS,
						zif_package_get_id (item->package));

			/* check this item */
			ret = zif_transaction_resolve_conflicts_item (data, item, &error_local);
			if (!ret) {
				if (skip_broken) {
					g_debug ("ignoring error as we're skip-broken: %s",
						 error_local->message);
					g_ptr_array_remove (priv->install, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				g_propagate_error (error, error_local);
				goto out;
			}
		}
	}

	/* anything to do? */
	items_success = zif_transaction_get_array_success (priv->install);
	items_success += zif_transaction_get_array_success (priv->remove);

	/* anything to do? */
	if (items_success == 0) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_NOTHING_TO_DO,
				     "no packages will be installed, removed or updated");
		goto out;
	}

	/* sort the install and remove arrays */
	g_ptr_array_sort (priv->install, (GCompareFunc) zif_transaction_item_sort_cb);
	g_ptr_array_sort (priv->remove, (GCompareFunc) zif_transaction_item_sort_cb);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	priv->state = ZIF_TRANSACTION_STATE_RESOLVED;
	g_debug ("done depsolve");
	zif_transaction_show_array ("installing", priv->install);
	zif_transaction_show_array ("removing", priv->remove);
	ret = TRUE;
out:
	if (data != NULL) {
		g_object_unref (data->state);
		g_free (data);
	}
	return ret;
}

/**
 * zif_transaction_prepare:
 * @transaction: the #ZifTransaction object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Prepares the transaction ensuring all packages are downloaded.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.1.3
 **/
gboolean
zif_transaction_prepare (ZifTransaction *transaction, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	guint i;
	ZifTransactionItem *item;
	ZifPackage *package;
	GPtrArray *download = NULL;
	GError *error_local = NULL;
	ZifState *state_loop;
	ZifState *state_local;
	const gchar *cache_filename;
	ZifTransactionPrivate *priv;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get private */
	priv = transaction->priv;

	/* is valid */
	if (priv->state != ZIF_TRANSACTION_STATE_RESOLVED) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "not in resolve state, instead is %s",
			     zif_transaction_state_to_string (priv->state));
		goto out;
	}

	/* nothing to download */
	if (priv->install->len == 0) {
		priv->state = ZIF_TRANSACTION_STATE_PREPARED;
		ret = TRUE;
		goto out;
	}

	/* set steps */
	ret = zif_state_set_steps (state,
				   error,
				   10, /* check downloads exist */
				   90, /* download them */
				   -1);
	if (!ret)
		goto out;

	/* check if the packages need downloading */
	download = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, priv->install->len);
	for (i=0; i<priv->install->len; i++) {
		item = g_ptr_array_index (priv->install, i);

		/* this is a meta package in make check */
		if (ZIF_IS_PACKAGE_META (item->package)) {
			g_debug ("no processing %s in the test suite",
				 zif_package_get_id (item->package));
			goto skip;
		}

		/* this is a package file we're local-installing */
		if (ZIF_IS_PACKAGE_LOCAL (item->package)) {
			g_debug ("no processing %s as it's already local",
				 zif_package_get_id (item->package));
			goto skip;
		}

		/* see if download already exists */
		g_debug ("checking %s",
			 zif_package_get_id (item->package));
		zif_state_action_start (state,
					ZIF_STATE_ACTION_CHECKING,
					zif_package_get_name (item->package));
		state_loop = zif_state_get_child (state_local);
		cache_filename = zif_package_get_cache_filename (item->package,
								 state_loop,
								 &error_local);
		if (cache_filename == NULL) {
			ret = FALSE;
			g_propagate_prefixed_error (error, error_local,
						    "cannot check download %s: ",
						    zif_package_get_id (item->package));
			goto out;
		}

		/* doesn't exist, so add to the list */
		if (!g_file_test (cache_filename, G_FILE_TEST_EXISTS)) {
			g_debug ("add to dowload queue %s",
				 zif_package_get_id (item->package));
			g_ptr_array_add (download, g_object_ref (item->package));
		} else {
			g_debug ("package %s is already downloaded",
				 zif_package_get_id (item->package));
		}
skip:
		/* done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* download files */
	if (download->len > 0) {
		state_local = zif_state_get_child (state);
		zif_state_set_number_steps (state_local, download->len);
		for (i=0; i<download->len; i++) {
			package = g_ptr_array_index (download, i);
			state_loop = zif_state_get_child (state_local);
			g_debug ("downloading %s",
				 zif_package_get_id (package));
			ret = zif_package_remote_download (ZIF_PACKAGE_REMOTE (package), NULL, state_loop, &error_local);
			if (!ret) {
				g_propagate_prefixed_error (error, error_local,
							    "cannot download %s: ",
							    zif_package_get_id (package));
				goto out;
			}

			/* done */
			ret = zif_state_done (state_local, error);
			if (!ret)
				goto out;
		}
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	priv->state = ZIF_TRANSACTION_STATE_PREPARED;
out:
	if (download != NULL)
		g_ptr_array_unref (download);
	return ret;
}

typedef enum {
	ZIF_TRANSACTION_STEP_STARTED,
	ZIF_TRANSACTION_STEP_PREPARING,
	ZIF_TRANSACTION_STEP_WRITING
} ZifTransactionStep;

typedef struct {
	ZifTransaction		*transaction;
	ZifState		*state;
	ZifState		*child;
	rpmts			 ts;
	FD_t			 fd;
	ZifTransactionStep	 step;
} ZifTransactionCommit;

/**
 * zif_transaction_get_item_from_cache_filename_suffix:
 **/
static ZifTransactionItem *
zif_transaction_get_item_from_cache_filename_suffix (GPtrArray *array,
						     const gchar *filename_suffix)
{
	guint i;
	const gchar *cache_filename;
	ZifTransactionItem *item;
	ZifState *state;

	/* this is safe as the cache value will already be hot */
	state = zif_state_new ();
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* get filename */
		zif_state_reset (state);
		cache_filename = zif_package_get_cache_filename (item->package,
								 state,
								 NULL);
		if (cache_filename == NULL)
			continue;

		/* success */
		if (g_str_has_suffix (cache_filename,
				      filename_suffix)) {
			goto out;
		}
	}

	/* failure */
	item = NULL;
out:
	g_object_unref (state);
	return item;
}


/**
 * zif_transaction_ts_progress_cb:
 **/
static void *
zif_transaction_ts_progress_cb (const void *arg,
				const rpmCallbackType what,
				const rpm_loff_t amount,
				const rpm_loff_t total,
				fnpyKey key, void *data)
{
	const char *filename = (const char *) key;
	gboolean ret;
	GError *error_local = NULL;
	void *rc = NULL;
	ZifStateAction action;
	ZifTransactionItem *item;

	ZifTransactionCommit *commit = (ZifTransactionCommit *) data;

	switch (what) {
	case RPMCALLBACK_INST_OPEN_FILE:

		/* valid? */
		if (filename == NULL || filename[0] == '\0')
			return NULL;

		/* open the file and return file descriptor */
		commit->fd = Fopen (filename, "r.ufdio");
		return (void *) commit->fd;
		break;

	case RPMCALLBACK_INST_CLOSE_FILE:

		/* just close the file */
		if (commit->fd != NULL) {
			Fclose (commit->fd);
			commit->fd = NULL;
		}
		break;

	case RPMCALLBACK_INST_START:

		/* find item */
		item = zif_transaction_get_item_from_cache_filename_suffix (commit->transaction->priv->install,
									    filename);
		if (item == NULL)
			g_assert_not_reached ();

		/* map to correct action code */
		action = ZIF_STATE_ACTION_INSTALLING;
		if (item->reason == ZIF_TRANSACTION_REASON_INSTALL_FOR_UPDATE)
			action = ZIF_STATE_ACTION_UPDATING;

		/* install start */
		commit->step = ZIF_TRANSACTION_STEP_WRITING;
		commit->child = zif_state_get_child (commit->state);
		zif_state_action_start (commit->child,
					action,
					zif_package_get_id (item->package));
		g_debug ("install start: %s size=%i", filename, (gint32) total);
		break;

	case RPMCALLBACK_UNINST_START:

		/* find item */
		item = zif_transaction_get_item_from_cache_filename_suffix (commit->transaction->priv->remove,
									    filename);
		if (item == NULL)
			g_assert_not_reached ();

		/* map to correct action code */
		action = ZIF_STATE_ACTION_REMOVING;
		if (item->reason == ZIF_TRANSACTION_REASON_REMOVE_FOR_UPDATE)
			action = ZIF_STATE_ACTION_CLEANING;

		/* remove start */
		commit->step = ZIF_TRANSACTION_STEP_WRITING;
		commit->child = zif_state_get_child (commit->state);
		zif_state_action_start (commit->child,
					action,
					zif_package_get_id (item->package));
		g_debug ("remove start: %s size=%i", filename, (gint32) total);
		break;

	case RPMCALLBACK_TRANS_PROGRESS:
	case RPMCALLBACK_INST_PROGRESS:
	case RPMCALLBACK_UNINST_PROGRESS:

		/* we're preparing the transaction */
		if (commit->step == ZIF_TRANSACTION_STEP_PREPARING) {
			g_debug ("ignoring preparing %i / %i",
				 (gint32) amount, (gint32) total);
			break;
		}

		/* progress */
		g_debug ("progress %i/%i", (gint32) amount, (gint32) total);
		zif_state_set_percentage (commit->child, (100.0f / (gfloat) total) * (gfloat) amount);
		if (amount == total) {
			ret = zif_state_done (commit->state, &error_local);
			if (!ret) {
				g_warning ("state increment failed: %s",
					   error_local->message);
				g_error_free (error_local);
			}
		}
		break;

	case RPMCALLBACK_TRANS_START:

		/* we setup the state */
		g_debug ("preparing transaction with %i items", (gint32) total);
		zif_state_set_number_steps (commit->state, total);
		commit->step = ZIF_TRANSACTION_STEP_PREPARING;
		break;

	case RPMCALLBACK_TRANS_STOP:

		/* don't do anything */
		g_debug ("transaction stop");
		break;

	case RPMCALLBACK_UNINST_STOP:

		/* no idea */
		g_debug ("uninstall done");
		ret = zif_state_done (commit->state, &error_local);
		if (!ret) {
			g_warning ("state increment failed: %s",
				   error_local->message);
			g_error_free (error_local);
		}
		break;

	case RPMCALLBACK_UNPACK_ERROR:
	case RPMCALLBACK_CPIO_ERROR:
	case RPMCALLBACK_SCRIPT_ERROR:
	case RPMCALLBACK_UNKNOWN:
	case RPMCALLBACK_REPACKAGE_PROGRESS:
	case RPMCALLBACK_REPACKAGE_START:
	case RPMCALLBACK_REPACKAGE_STOP:
		g_debug ("something uninteresting");
		break;
	default:
		g_warning ("something else entirely");
		break;
	}

	return rc;
}

/**
 * zif_transaction_add_install_to_ts:
 **/
static gboolean
zif_transaction_add_install_to_ts (ZifTransactionCommit *commit,
				   ZifPackage *package,
				   ZifState *state,
				   GError **error)
{
	gint res;
	const gchar *cache_filename;
	Header hdr;
	FD_t fd;
	gboolean ret = FALSE;

	/* get the local file */
	cache_filename = zif_package_get_cache_filename (package,
							 state,
							 error);
	if (cache_filename == NULL)
		goto out;

	/* open this */
	fd = Fopen (cache_filename, "r.ufdio");
	res = rpmReadPackageFile (commit->ts, fd, NULL, &hdr);
	Fclose (fd);
	if (res != RPMRC_OK) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to open: %s [%i]",
			     cache_filename, res);
		goto out;
	}

	/* add to the transaction */
	res = rpmtsAddInstallElement (commit->ts, hdr, (fnpyKey) cache_filename, 0, NULL);
	if (res != 0) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to add install element: %s [%i]",
			     cache_filename, res);
		goto out;
	}
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_transaction_rpm_verbosity_string_to_value:
 **/
static gint
zif_transaction_rpm_verbosity_string_to_value (const gchar *value)
{
	if (g_strcmp0 (value, "critical") == 0)
		return RPMLOG_CRIT;
	if (g_strcmp0 (value, "emergency") == 0)
		return RPMLOG_EMERG;
	if (g_strcmp0 (value, "error") == 0)
		return RPMLOG_ERR;
	if (g_strcmp0 (value, "warn") == 0)
		return RPMLOG_WARNING;
	if (g_strcmp0 (value, "debug") == 0)
		return RPMLOG_DEBUG;
	return RPMLOG_EMERG;
}

/**
 * zif_transaction_write_log:
 **/
static gboolean
zif_transaction_write_log (ZifTransaction *transaction, GError **error)
{
	gboolean ret = FALSE;
	gchar *filename = NULL;
	GFile *file = NULL;
	GFileIOStream *stream = NULL;
	GOutputStream *output = NULL;
	GString *data = NULL;
	guint i;
	ZifTransactionItem *item;

	/* open up log file */
	filename = zif_config_get_string (transaction->priv->config,
					  "logfile", error);
	if (filename == NULL)
		goto out;
	file = g_file_new_for_path (filename);
	g_debug ("writing to file: %s", filename);
	stream = g_file_open_readwrite (file, NULL, error);
	if (stream == NULL)
		goto out;

	/* format data */
	data = g_string_new ("");
	for (i=0; i<transaction->priv->install->len; i++) {
		item = g_ptr_array_index (transaction->priv->install, i);
		if (item->cancelled)
			continue;
		g_string_append_printf (data, "Zif: [install] %s (%s)\n",
					zif_package_get_id (item->package),
					zif_transaction_reason_to_string (item->reason));
	}
	for (i=0; i<transaction->priv->remove->len; i++) {
		item = g_ptr_array_index (transaction->priv->remove, i);
		if (item->cancelled)
			continue;
		g_string_append_printf (data, "Zif: [remove] %s (%s)\n",
					zif_package_get_id (item->package),
					zif_transaction_reason_to_string (item->reason));
	}

	/* write data */
	g_debug ("writing %s", data->str);
	output = g_io_stream_get_output_stream (G_IO_STREAM (stream));
	if (output == NULL) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "cannot get output stream for %s",
			     filename);
		goto out;
	}
	ret = g_seekable_seek (G_SEEKABLE (output),
			       0, G_SEEK_END,
			       NULL, error);
	if (!ret)
		goto out;
	ret = g_output_stream_write_all (output,
					 data->str,
					 data->len,
					 NULL,
					 NULL,
					 error);
	if (!ret)
		goto out;
	ret = g_output_stream_close (output, NULL, error);
	if (!ret)
		goto out;
out:
	g_free (filename);
	if (data != NULL)
		g_string_free (data, TRUE);
	if (file != NULL)
		g_object_unref (file);
	if (stream != NULL)
		g_object_unref (stream);
	return ret;
}

/**
 * zif_transaction_delete_packages:
 **/
static gboolean
zif_transaction_delete_packages (ZifTransaction *transaction, ZifState *state, GError **error)
{
	GFile *file;
	guint i;
	guint ret = TRUE;
	ZifState *state_local;
	ZifState *state_loop;
	ZifTransactionItem *item;
	ZifTransactionPrivate *priv = transaction->priv;

	/* nothing to delete? */
	if (priv->install->len == 0)
		goto out;

	/* delete each downloaded file */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, priv->install->len);
	for (i=0; i<priv->install->len; i++) {
		item = g_ptr_array_index (priv->install, i);

		/* delete the cache file */
		state_loop = zif_state_get_child (state_local);
		file = zif_package_get_cache_file (item->package,
						   state_loop,
						   error);
		ret = g_file_delete (file, NULL, error);
		if (!ret)
			goto out;

		/* this part done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * zif_transaction_commit:
 * @transaction: the #ZifTransaction object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Commits all the changes to disk.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.1.3
 **/
gboolean
zif_transaction_commit (ZifTransaction *transaction, ZifState *state, GError **error)
{
	const gchar *prefix;
	gboolean keep_cache;
	gboolean ret = FALSE;
	gchar *verbosity_string = NULL;
	gint flags;
	gint rc;
	gint retval;
	gint verbosity;
	guint i;
	Header hdr;
	rpmdb db = NULL;
	rpmprobFilterFlags problems_filter = 0;
	rpmps probs = NULL;
	ZifState *state_local;
	ZifState *state_loop;
	ZifTransactionCommit *commit = NULL;
	ZifTransactionItem *item;
	ZifTransactionPrivate *priv;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get private */
	priv = transaction->priv;

	/* is valid */
	if (priv->state != ZIF_TRANSACTION_STATE_PREPARED) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "not in prepared state, instead is %s",
			     zif_transaction_state_to_string (priv->state));
		goto out;
	}

	/* set state */
	ret = zif_state_set_steps (state,
				   error,
				   2, /* install */
				   2, /* remove */
				   91, /* commit */
				   2, /* write log */
				   3, /* delete files */
				   -1);
	if (!ret)
		goto out;
	zif_state_action_start (state, ZIF_STATE_ACTION_PREPARING, NULL);

	/* get verbosity from the config file */
	verbosity_string = zif_config_get_string (priv->config, "rpmverbosity", NULL);
	verbosity = zif_transaction_rpm_verbosity_string_to_value (verbosity_string);
	rpmSetVerbosity (verbosity);

	/* we can't get this from ZifStoreLocal, as when the db is changed,
	 * it is marked dirty and unloaded */
	prefix = zif_store_local_get_prefix (ZIF_STORE_LOCAL (priv->store_local));
	retval = rpmdbOpen (prefix, &db, O_RDWR, 0777);
	if (retval != 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to open rpmdb");
		ret = FALSE;
		goto out;
	}

	/* setup the transaction */
	commit = g_new0 (ZifTransactionCommit, 1);
	commit->ts = rpmtsCreate ();
	commit->transaction = transaction;
	rpmtsSetRootDir (commit->ts, prefix);
	rpmtsSetNotifyCallback (commit->ts, zif_transaction_ts_progress_cb, commit);

	flags = rpmtsSetVSFlags (commit->ts, _RPMVSF_NOSIGNATURES | _RPMVSF_NODIGESTS);

	/* add things to install */
	state_local = zif_state_get_child (state);
	if (priv->install->len > 0)
		zif_state_set_number_steps (state_local,
					    priv->install->len);
	for (i=0; i<priv->install->len; i++) {
		item = g_ptr_array_index (priv->install, i);

		/* add the install */
		state_loop = zif_state_get_child (state_local);
		ret = zif_transaction_add_install_to_ts (commit,
							 item->package,
							 state_loop,
							 error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* add things to remove */
	for (i=0; i<priv->remove->len; i++) {
		item = g_ptr_array_index (priv->remove, i);

		/* remove it */
		hdr = zif_package_local_get_header (ZIF_PACKAGE_LOCAL (item->package));
		retval = rpmtsAddEraseElement (commit->ts, hdr, -1);
		if (retval != 0) {
			ret = FALSE;
			g_set_error (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_FAILED,
				     "could not add erase element (%i)",
				     retval);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	rpmtsSetVSFlags (commit->ts, flags);
	rpmtsSetFlags (commit->ts, RPMTRANS_FLAG_NONE);
	commit->state = zif_state_get_child (state);

	/* filter diskspace */
	if (!zif_config_get_boolean (priv->config, "diskspacecheck", NULL))
		problems_filter += RPMPROB_FILTER_DISKSPACE;

	/* run the transaction */
	rc = rpmtsRun (commit->ts, NULL, RPMPROB_FILTER_NONE);
	if (rc < 0) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Error %i running transaction", rc);
		goto out;
	}
	if (rc > 0) {
		rpmpsi psi;
		rpmProblem prob;
		const gchar *msg;

		/* get a list of problems */
		probs = rpmtsProblems (commit->ts);

		/* parse problems */
		psi = rpmpsInitIterator (probs);
		while (rpmpsNextIterator (psi) >= 0) {
			prob = rpmpsGetProblem (psi);
			msg = rpmProblemGetStr (prob);
			g_warning ("problem: %s", msg);
		}
		rpmpsFreeIterator (psi);

		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Error running transaction. check stdout");
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* append to the config file */
	ret = zif_transaction_write_log (transaction, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* remove the files we downloaded */
	keep_cache = zif_config_get_boolean (priv->config, "keepcache", NULL);
	if (!keep_cache) {
		state_local = zif_state_get_child (state);
		ret = zif_transaction_delete_packages (transaction,
						       state_local,
						       error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	priv->state = ZIF_TRANSACTION_STATE_COMMITTED;
	g_debug ("Done!");
out:
	g_free (verbosity_string);
	if (probs != NULL)
		rpmpsFree (probs);
	if (db != NULL)
		rpmdbClose (db);
	if (commit != NULL) {
		if (commit->ts != NULL)
			rpmtsFree (commit->ts);
		g_free (commit);
	}
	return ret;
}

/**
 * zif_transaction_set_store_local:
 * @transaction: the #ZifTransaction object
 * @store: the #ZifStore to use for installed packages
 *
 * Sets the local store for use in the transaction.
 *
 * Since: 0.1.3
 **/
void
zif_transaction_set_store_local (ZifTransaction *transaction, ZifStore *store)
{
	g_return_if_fail (ZIF_IS_TRANSACTION (transaction));
	g_return_if_fail (ZIF_IS_STORE (store));
	g_return_if_fail (transaction->priv->store_local == NULL);
	transaction->priv->store_local = g_object_ref (store);
}

/**
 * zif_transaction_set_stores_remote:
 * @transaction: the #ZifTransaction object
 * @stores: an array of #ZifStore's to use for available packages
 *
 * Sets the remote store for use in the transaction.
 *
 * Since: 0.1.3
 **/
void
zif_transaction_set_stores_remote (ZifTransaction *transaction, GPtrArray *stores)
{
	g_return_if_fail (ZIF_IS_TRANSACTION (transaction));
	g_return_if_fail (stores != NULL);
	g_return_if_fail (transaction->priv->stores_remote == NULL);
	transaction->priv->stores_remote = g_ptr_array_ref (stores);
}

/**
 * zif_transaction_set_verbose:
 * @transaction: the #ZifTransaction object
 * @verbose: if an insane amount of debugging should be printed
 *
 * Sets the printing policy for the transaction. You only need to set
 * this to true if you are debugging a problem with the depsolver.
 *
 * Since: 0.1.3
 **/
void
zif_transaction_set_verbose (ZifTransaction *transaction, gboolean verbose)
{
	g_return_if_fail (ZIF_IS_TRANSACTION (transaction));
	transaction->priv->verbose = verbose;
}


/**
 * zif_transaction_get_state:
 * @transaction: the #ZifTransaction object
 * @package: the #ZifPackage object
 *
 * Gets the reason why the package is in the install or remove array.
 *
 * Return value: A #ZifTransactionState enumerated value, or %ZIF_TRANSACTION_STATE_INVALID for error.
 *
 * Since: 0.1.3
 **/
ZifTransactionState
zif_transaction_get_state (ZifTransaction *transaction)
{
	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), ZIF_TRANSACTION_STATE_INVALID);
	return transaction->priv->state;
}

/**
 * zif_transaction_reset:
 * @transaction: the #ZifTransaction object
 *
 * Clears any pending or completed packages and returns the transaction
 * to the default state.
 *
 * Since: 0.1.3
 **/
void
zif_transaction_reset (ZifTransaction *transaction)
{
	g_return_if_fail (ZIF_IS_TRANSACTION (transaction));
	g_ptr_array_set_size (transaction->priv->install, 0);
	g_ptr_array_set_size (transaction->priv->update, 0);
	g_ptr_array_set_size (transaction->priv->remove, 0);
	g_hash_table_remove_all (transaction->priv->install_hash);
	g_hash_table_remove_all (transaction->priv->update_hash);
	g_hash_table_remove_all (transaction->priv->remove_hash);
	transaction->priv->state = ZIF_TRANSACTION_STATE_CLEAN;
}

/**
 * zif_transaction_finalize:
 **/
static void
zif_transaction_finalize (GObject *object)
{
	ZifTransaction *transaction;
	g_return_if_fail (ZIF_IS_TRANSACTION (object));
	transaction = ZIF_TRANSACTION (object);

	g_object_unref (transaction->priv->config);
	g_ptr_array_unref (transaction->priv->install);
	g_ptr_array_unref (transaction->priv->update);
	g_ptr_array_unref (transaction->priv->remove);
	g_hash_table_destroy (transaction->priv->install_hash);
	g_hash_table_destroy (transaction->priv->update_hash);
	g_hash_table_destroy (transaction->priv->remove_hash);
	if (transaction->priv->store_local != NULL)
		g_object_unref (transaction->priv->store_local);
	if (transaction->priv->stores_remote != NULL)
		g_ptr_array_unref (transaction->priv->stores_remote);

	G_OBJECT_CLASS (zif_transaction_parent_class)->finalize (object);
}

/**
 * zif_transaction_class_init:
 **/
static void
zif_transaction_class_init (ZifTransactionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_transaction_finalize;
	g_type_class_add_private (klass, sizeof (ZifTransactionPrivate));
}

/**
 * zif_transaction_init:
 **/
static void
zif_transaction_init (ZifTransaction *transaction)
{
	transaction->priv = ZIF_TRANSACTION_GET_PRIVATE (transaction);

	transaction->priv->config = zif_config_new ();

	/* packages we want to install */
	transaction->priv->install = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_transaction_item_free);
	transaction->priv->install_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	/* packages we want to update */
	transaction->priv->update = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_transaction_item_free);
	transaction->priv->update_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	/* packages we want to remove */
	transaction->priv->remove = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_transaction_item_free);;
	transaction->priv->remove_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * zif_transaction_new:
 *
 * Return value: A new #ZifTransaction class instance.
 *
 * Since: 0.1.3
 **/
ZifTransaction *
zif_transaction_new (void)
{
	ZifTransaction *transaction;
	transaction = g_object_new (ZIF_TYPE_TRANSACTION, NULL);
	return ZIF_TRANSACTION (transaction);
}
