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

#include "zif-transaction.h"
#include "zif-depend.h"
#include "zif-store.h"
#include "zif-store-array.h"

#define ZIF_TRANSACTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_TRANSACTION, ZifTransactionPrivate))

struct _ZifTransactionPrivate
{
	GPtrArray		*install;
	GPtrArray		*update;
	GPtrArray		*remove;
	ZifStore		*store_local;
	GPtrArray		*stores_remote;
	gboolean		 skip_broken;
	gboolean		 verbose;
	ZifTransactionState	 state;
};

typedef struct {
	ZifState		*state;
	ZifTransaction		*transaction;
	gboolean		 unresolved_dependencies;
} ZifTransactionResolve;

G_DEFINE_TYPE (ZifTransaction, zif_transaction, G_TYPE_OBJECT)

typedef struct {
	ZifPackage		*package;
	ZifPackage		*update_package; /* allows us to remove packages added or removed if the update failed */
	gboolean		 resolved;
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
 * @reason: the #ZifTransactionReason enum, e.g. %ZIF_TRANSACTION_REASON_USER_ACTION
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
	if (reason == ZIF_TRANSACTION_REASON_USER_ACTION)
		return "user-action";
	if (reason == ZIF_TRANSACTION_REASON_INSTALL_ONLYN)
		return "install-onlyn";
	if (reason == ZIF_TRANSACTION_REASON_UPDATE)
		return "update";
	if (reason == ZIF_TRANSACTION_REASON_INSTALL_DEPEND)
		return "install-depend";
	if (reason == ZIF_TRANSACTION_REASON_OBSOLETE)
		return "obsolete";
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
 * zif_transaction_get_update:
 * @transaction: the #ZifTransaction object
 *
 * Gets the list of packages to be updated.
 *
 * Return value: A #GPtrArray of #ZifPackages, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_transaction_get_update (ZifTransaction *transaction)
{
	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), NULL);
	return zif_transaction_get_package_array (transaction->priv->update);
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
	if (item->update_package)
		g_object_unref (item->update_package);
	g_free (item);
}

/**
 * zif_transaction_get_item_from_array:
 **/
static ZifTransactionItem *
zif_transaction_get_item_from_array (GPtrArray *array, ZifPackage *package)
{
	ZifTransactionItem *item;
	guint i;
	const gchar *package_id;

	/* find a package that matches */
	package_id = zif_package_get_id (package);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (g_strcmp0 (zif_package_get_id (item->package), package_id) == 0)
			return item;
	}
	return NULL;
}

/**
 * zif_transaction_get_item_from_array_by_update_package:
 **/
static ZifTransactionItem *
zif_transaction_get_item_from_array_by_update_package (GPtrArray *array,
						       ZifPackage *package)
{
	ZifTransactionItem *item;
	guint i;
	const gchar *package_id;

	/* find a package that matches */
	package_id = zif_package_get_id (package);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->update_package == NULL)
			continue;
		if (g_strcmp0 (zif_package_get_id (item->update_package), package_id) == 0)
			return item;
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
	item = zif_transaction_get_item_from_array (transaction->priv->install, package);
	if (item != NULL)
		return item->reason;
	item = zif_transaction_get_item_from_array (transaction->priv->remove, package);
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
 * zif_transaction_add_to_array:
 **/
static gboolean
zif_transaction_add_to_array (GPtrArray *array,
			      ZifPackage *package,
			      ZifPackage *update_package,
			      ZifTransactionReason reason)
{
	gboolean ret = FALSE;
	ZifTransactionItem *item;

	/* already added? */
	item = zif_transaction_get_item_from_array (array, package);
	if (item != NULL)
		goto out;

	/* create new item */
	item = g_new0 (ZifTransactionItem, 1);
	item->resolved = FALSE;
	item->reason = reason;
	item->package = g_object_ref (package);
	if (update_package != NULL)
		item->update_package = g_object_ref (update_package);
	g_ptr_array_add (array, item);

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_transaction_add_install_internal:
 **/
static gboolean
zif_transaction_add_install_internal (ZifTransaction *transaction,
				      ZifPackage *package,
				      ZifPackage *update_package,
				      ZifTransactionReason reason,
				      GError **error)
{
	gboolean ret;

	/* add to install */
	ret = zif_transaction_add_to_array (transaction->priv->install,
					    package,
					    update_package,
					    reason);
	if (!ret) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Not adding already added package for install %s",
			     zif_package_get_id (package));
		goto out;
	}

	g_debug ("Add INSTALL %s [%s] (with update package %s)",
		 zif_package_get_id (package),
		 zif_transaction_reason_to_string (reason),
		 update_package != NULL ? zif_package_get_id (update_package) : "none");
out:
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
						    ZIF_TRANSACTION_REASON_USER_ACTION,
						    error);
	return ret;
}

/**
 * zif_transaction_add_update_internal:
 **/
static gboolean
zif_transaction_add_update_internal (ZifTransaction *transaction,
				     ZifPackage *package,
				     ZifPackage *update_package,
				     ZifTransactionReason reason,
				     GError **error)
{
	gboolean ret;

	/* add to update */
	ret = zif_transaction_add_to_array (transaction->priv->update,
					    package,
					    update_package,
					    reason);
	if (!ret) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Not adding already added package for update %s",
			     zif_package_get_id (package));
		goto out;
	}

	g_debug ("Add UPDATE %s [%s] (with update package %s)",
		 zif_package_get_id (package),
		 zif_transaction_reason_to_string (reason),
		 update_package != NULL ? zif_package_get_id (update_package) : "none");
out:
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
						   ZIF_TRANSACTION_REASON_USER_ACTION,
						   error);
	return ret;
}

/**
 * zif_transaction_add_remove_internal:
 **/
static gboolean
zif_transaction_add_remove_internal (ZifTransaction *transaction,
				     ZifPackage *package,
				     ZifPackage *update_package,
				     ZifTransactionReason reason,
				     GError **error)
{
	gboolean ret;

	/* add to remove */
	ret = zif_transaction_add_to_array (transaction->priv->remove,
					    package,
					    update_package,
					    reason);
	if (!ret) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Not adding already added package for remove %s",
			     zif_package_get_id (package));
		goto out;
	}

	g_debug ("Add REMOVE %s [%s] (with update package %s)",
		 zif_package_get_id (package),
		 zif_transaction_reason_to_string (reason),
		 update_package != NULL ? zif_package_get_id (update_package) : "none");
out:
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
						   ZIF_TRANSACTION_REASON_USER_ACTION,
						   error);
	return ret;
}

/**
 * zif_transaction_package_file_depend:
 **/
static gboolean
zif_transaction_package_file_depend (ZifPackage *package,
				     const gchar *filename,
				     ZifDepend **satisfies,
				     ZifState *state,
				     GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	const gchar *filename_tmp;
	GPtrArray *provides;
	guint i;

	/* get files */
	zif_state_reset (state);
	provides = zif_package_get_files (package, state, &error_local);
	if (provides == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get files for %s: %s",
			     zif_package_get_id (package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* search array */
	if (0) {
		g_debug ("%i files for %s",
			 provides->len,
			 zif_package_get_id (package));
		for (i=0; i<provides->len; i++) {
			filename_tmp = g_ptr_array_index (provides, i);
			g_debug ("require: %s:%s", filename_tmp, filename);
		}
	}
	for (i=0; i<provides->len; i++) {
		filename_tmp = g_ptr_array_index (provides, i);
		if (g_strcmp0 (filename_tmp, filename) == 0) {
			*satisfies = zif_depend_new ();
			zif_depend_set_flag (*satisfies, ZIF_DEPEND_FLAG_ANY);
			zif_depend_set_name (*satisfies, filename);
			goto out;
		}
	}

	/* success, but did not find */
	*satisfies = NULL;
out:
	return ret;
}

/**
 * zif_transaction_package_provides:
 * @satisfies: the matched depend, free with g_object_unref() in for not %NULL
 **/
static gboolean
zif_transaction_package_provides (ZifPackage *package,
				  ZifDepend *depend,
				  ZifDepend **satisfies,
				  ZifState *state,
				  GError **error)
{
	const gchar *depend_description;
	gboolean ret = TRUE;
	GError *error_local = NULL;
	GPtrArray *provides = NULL;
	guint i;
	ZifDepend *depend_tmp;

	/* is this a file require */
	if (zif_depend_get_name (depend)[0] == '/') {
		ret = zif_transaction_package_file_depend (package,
							   zif_depend_get_name (depend),
							   satisfies,
							   state,
							   error);
		goto out;
	}

	/* get the list of provides (which is cached after the first access) */
	zif_state_reset (state);
	provides = zif_package_get_provides (package, state, &error_local);
	if (provides == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get provides for %s: %s",
			     zif_package_get_id (package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* find what we're looking for */
	depend_description = zif_depend_get_description (depend);
	for (i=0; i<provides->len; i++) {
		depend_tmp = g_ptr_array_index (provides, i);
		ret = zif_depend_satisfies (depend, depend_tmp);
		if (ret) {
			*satisfies = g_object_ref (depend_tmp);
			goto out;
		}
	}

	/* success, but did not find */
	ret = TRUE;
	*satisfies = NULL;
out:
	if (provides != NULL)
		g_ptr_array_unref (provides);
	return ret;
}

/**
 * zif_transaction_package_requires:
 *
 * Satisfies is the dep that satisfies the query. Free with g_object_unref().
 **/
static gboolean
zif_transaction_package_requires (ZifPackage *package,
				  ZifDepend *depend,
				  ZifDepend **satisfies,
				  ZifState *state,
				  GError **error)
{
	const gchar *depend_description;
	gboolean ret = TRUE;
	GError *error_local = NULL;
	GPtrArray *requires;
	guint i;
	ZifDepend *depend_tmp;

	/* get the list of requires (which is cached after the first access) */
	if (0) {
		g_debug ("Find out if %s requires %s",
			 zif_package_get_id (package),
			 zif_depend_get_description (depend));
	}
	zif_state_reset (state);
	requires = zif_package_get_requires (package, state, &error_local);
	if (requires == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get requires for %s: %s",
			     zif_package_get_id (package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* find what we're looking for */
	if (0) {
		g_debug ("got %i requires for %s",
			 requires->len,
			 zif_package_get_id (package));
		for (i=0; i<requires->len; i++) {
			depend_tmp = g_ptr_array_index (requires, i);
			g_debug ("%i.\t%s",
				 i+1,
				 zif_depend_get_description (depend_tmp));
		}
	}
	depend_description = zif_depend_get_description (depend);
	for (i=0; i<requires->len; i++) {
		depend_tmp = g_ptr_array_index (requires, i);
		if (zif_depend_satisfies (depend, depend_tmp)) {
			g_debug ("%s satisfied by %s",
				 zif_depend_get_description (depend_tmp),
				 zif_package_get_id (package));
			ret = TRUE;
			*satisfies = g_object_ref (depend_tmp);
			goto out;
		}
	}

	/* success, but did not find */
	ret = TRUE;
	*satisfies = NULL;
out:
	if (requires != NULL)
		g_ptr_array_unref (requires);
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
		ret = zif_transaction_package_provides (item->package,
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
 * _zif_package_array_filter_packages_by_best_arch:
 *
 * If we have the following packages:
 *  - glibc.i386
 *  - hal.i386
 *  - glibc.i686
 *
 * Then we output:
 *  - glibc.i686
 *
 **/
static void
_zif_package_array_filter_packages_by_best_arch (GPtrArray *array)
{
	ZifPackage *package;
	guint i;
	const gchar *arch;
	const gchar *best_arch = NULL;

	/* find the best arch */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		arch = zif_package_get_arch (package);
		if (g_strcmp0 (arch, "x86_64") == 0)
			break;
		if (g_strcmp0 (arch, best_arch) > 0) {
			best_arch = arch;
		}
	}

	/* if no obvious best, skip */
	g_debug ("bestarch=%s", best_arch);
	if (best_arch == NULL)
		return;

	/* remove any that are not best */
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		arch = zif_package_get_arch (package);
		if (g_strcmp0 (arch, best_arch) != 0) {
			g_ptr_array_remove_index_fast (array, i);
			continue;
		}
		i++;
	}
}

/**
 * _zif_package_array_filter_packages_by_smallest_name:
 *
 * If we have the following packages:
 *  - glibc.i386
 *  - hal.i386
 *
 * Then we output:
 *  - hal.i686
 *
 * As it has the smallest name. I know it's insane, but it's what yum does.
 *
 **/
static void
_zif_package_array_filter_packages_by_smallest_name (GPtrArray *array)
{
	ZifPackage *package;
	guint i;
	guint length;
	guint shortest = G_MAXUINT;

	/* find the smallest name */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		length = strlen (zif_package_get_name (package));
		if (length < shortest)
			shortest = length;
	}

	/* remove entries that are longer than the shortest name */
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		length = strlen (zif_package_get_name (package));
		if (length != shortest) {
			g_ptr_array_remove_index_fast (array, i);
			continue;
		}
		i++;
	}
}

/**
 * _zif_package_array_filter_packages_by_depend_version:
 **/
static gboolean
_zif_package_array_filter_packages_by_depend_version (GPtrArray *array,
						      ZifDepend *depend,
						      ZifState *state,
						      GError **error)
{
	guint i;
	gboolean ret = TRUE;
	ZifPackage *package;
	ZifDepend *satisfies = NULL;

	/* remove entries that do not satisfy the best dep */
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		ret = zif_transaction_package_provides (package, depend, &satisfies, state, error);
		if (!ret)
			goto out;
		if (satisfies == NULL) {
			g_ptr_array_remove_index_fast (array, i);
			continue;
		}
		g_object_unref (satisfies);
		i++;
	}
out:
	return ret;
}

/**
 * zif_transaction_get_package_provide_from_package_array:
 **/
static gboolean
zif_transaction_get_package_provide_from_package_array (GPtrArray *array,
							ZifDepend *depend,
							ZifPackage **package,
							ZifState *state,
							GError **error)
{
	gboolean ret = TRUE;
	guint i;
	ZifPackage *package_tmp;
	GPtrArray *satisfy_array;
	ZifDepend *satisfies = NULL;
	ZifDepend *best_depend = NULL;
	GError *error_local = NULL;

	/* interate through the array */
	satisfy_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array->len; i++) {
		package_tmp = g_ptr_array_index (array, i);

		/* does this match */
		ret = zif_transaction_package_provides (package_tmp, depend, &satisfies, state, error);
		if (!ret)
			goto out;

		/* gotcha, but keep looking */
		if (satisfies != NULL) {
			g_ptr_array_add (satisfy_array, g_object_ref (package_tmp));

			/* ensure we track the best depend */
			if (best_depend == NULL ||
			    zif_depend_compare (satisfies, best_depend) > 0) {
				if (best_depend != NULL)
					g_object_unref (best_depend);
				best_depend = g_object_ref (satisfies);
			}

			g_object_unref (satisfies);
		}
	}

	/* print what we've got */
	g_debug ("provide %s has %i matches",
		 zif_depend_get_description (depend),
		 satisfy_array->len);
	if (best_depend != NULL) {
		g_debug ("best depend was %s",
			 zif_depend_get_description (best_depend));
	}

	/* filter these down so we get best architectures listed first */
	if (satisfy_array->len > 1) {
		_zif_package_array_filter_packages_by_best_arch (satisfy_array);
		g_debug ("after filtering by arch, array now %i packages", satisfy_array->len);
	}

	/* if the depends are the same, choose the one with the biggest version */
	if (satisfy_array->len > 1) {
		ret = _zif_package_array_filter_packages_by_depend_version (satisfy_array,
									    best_depend,
									    state, error);
		if (!ret)
			goto out;
		g_debug ("after filtering by depend, array now %i packages", satisfy_array->len);
	}

	/* filter these down so we get smallest names listed first */
	if (satisfy_array->len > 1) {
		_zif_package_array_filter_packages_by_smallest_name (satisfy_array);
		g_debug ("after filtering by name length, array now %i packages", satisfy_array->len);
	}

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
	if (best_depend != NULL)
		g_object_unref (best_depend);
	g_ptr_array_unref (satisfy_array);
	return ret;
}

/**
 * zif_transaction_get_package_provide_from_store:
 *
 * Returns a package that provides something.
 **/
static gboolean
zif_transaction_get_package_provide_from_store (ZifStore *store,
						ZifDepend *depend,
						ZifPackage **package,
						ZifState *state,
						GError **error)
{
	GPtrArray *array;
	GError *error_local = NULL;
	gboolean ret = FALSE;

	/* get the package list */
	zif_state_reset (state);
	array = zif_store_get_packages (store, state, &error_local);
	if (array == NULL) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get installed package list: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* search it */
	ret = zif_transaction_get_package_provide_from_package_array (array,
								      depend,
								      package,
								      state,
								      error);
	if (!ret) {
		g_assert (error == NULL || *error != NULL);
		goto out;
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;

}

/**
 * zif_transaction_get_package_provide_from_store:
 *
 * Returns an array of packages that require something.
 **/
static gboolean
zif_transaction_get_package_requires_from_store (ZifStore *store,
						 ZifDepend *depend,
						 GPtrArray *already_marked_to_remove,
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
		item = zif_transaction_get_item_from_array (already_marked_to_remove,
							    package);
		if (item != NULL) {
			if (0) {
				g_debug ("not getting requires for %s, as already in remove array",
					 zif_package_get_id (package));
			}
			continue;
		}

		/* get requires */
		ret = zif_transaction_package_requires (package,
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
zif_transaction_get_packages_provides_from_store_array (GPtrArray *store_array,
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
		ret = zif_transaction_get_package_provide_from_store (store,
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
zif_transaction_get_package_provide_from_store_array (GPtrArray *store_array,
						      ZifDepend *depend,
						      ZifPackage **package,
						      ZifState *state,
						      GError **error)
{
	GPtrArray *array = NULL;
	gboolean ret;
	GError *error_local = NULL;

	/* get the list */
	ret = zif_transaction_get_packages_provides_from_store_array (store_array,
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
					GError **error)
{
	gboolean ret = TRUE;
	ZifPackage *package_provide = NULL;
	GPtrArray *already_installed = NULL;
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
	ret = zif_transaction_get_package_provide_from_store (data->transaction->priv->store_local,
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
	ret = zif_transaction_get_package_provide_from_store_array (data->transaction->priv->stores_remote,
								    depend, &package_provide,
								    data->state, error);
	if (!ret) {
		g_assert (error == NULL || *error != NULL);
		goto out;
	}
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

		/* remove old versions */
		for (i=0; i<already_installed->len; i++) {
			package = g_ptr_array_index (already_installed, i);
			g_debug ("%s is already installed, and we want %s, so removing installed version",
				 zif_package_get_id (package),
				 zif_package_get_id (package_provide));
			ret = zif_transaction_add_remove_internal (data->transaction,
								   package,
								   NULL,
								   ZIF_TRANSACTION_REASON_UPDATE,
								   error);
			if (!ret)
				goto out;
		}
skip_resolve:
		/* add the provide to the install set */
		ret = zif_transaction_add_install_internal (data->transaction,
							    package_provide,
							    NULL,
							    ZIF_TRANSACTION_REASON_INSTALL_DEPEND,
							    error);
		if (!ret)
			goto out;
		data->unresolved_dependencies = TRUE;
		goto out;
	}

	/* failed */
	ret = FALSE;
	g_set_error (error,
		     ZIF_TRANSACTION_ERROR,
		     ZIF_TRANSACTION_ERROR_FAILED,
		     "nothing provides %s",
		     zif_depend_get_description (depend));
out:
	if (already_installed != NULL)
		g_ptr_array_unref (already_installed);
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
	guint i;
	ZifDepend *depend;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifPackage *package_oldest = NULL;
	ZifTransactionItem *item_tmp;
	guint installonlyn = 1;

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

	/* kernel is special */
	if (g_strcmp0 (zif_package_get_name (item->package), "kernel") == 0 ||
	    g_strcmp0 (zif_package_get_name (item->package), "kernel-devel") == 0)
		installonlyn = 3;

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
		item_tmp = zif_transaction_get_item_from_array (data->transaction->priv->remove,
								package_oldest);
		if (item_tmp == NULL) {
			g_debug ("installing package %s would have %i versions installed (maximum %i) so removing %s",
				 zif_package_get_id (item->package),
				 array->len,
				 installonlyn,
				 zif_package_get_id (package_oldest));
			ret = zif_transaction_add_remove_internal (data->transaction,
								   package_oldest,
								   NULL,
								   ZIF_TRANSACTION_REASON_INSTALL_ONLYN,
								   error);
			if (!ret) {
				g_assert (error == NULL || *error != NULL);
				goto out;
			}
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
		ret = zif_transaction_resolve_install_depend (data, depend, error);
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
	GPtrArray *package_requires = NULL;
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
	local_provides = zif_store_what_provides (data->transaction->priv->store_local,
						  depend,
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
							       data->transaction->priv->remove,
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
		ret = zif_transaction_package_requires (package, depend, &satisfies, data->state, error);
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

		/* package is being updated, so try to update deps too */
		if (item->reason == ZIF_TRANSACTION_REASON_UPDATE) {
			ret = zif_transaction_add_update_internal (data->transaction,
								   package,
								   item->package,
								   item->reason,
								   error);
			if (!ret)
				goto out;

		} else {
			/* remove the package */
			ret = zif_transaction_add_remove_internal (data->transaction,
								   package,
								   NULL,
								   item->reason,
								   error);
			if (!ret)
				goto out;
		}
		data->unresolved_dependencies = TRUE;
	}
out:
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
	GPtrArray *files = NULL;
	guint i;
	GError *error_local = NULL;
	gboolean ret = FALSE;
	ZifDepend *depend;
	const gchar *filename;

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

	/* get filelist, as a file might be depending on one of its files */
	g_debug ("getting files for %s", zif_package_get_id (item->package));
	zif_state_reset (data->state);
	files = zif_package_get_files (item->package, data->state, &error_local);
	if (files == NULL) {
		g_set_error (error, ZIF_TRANSACTION_ERROR, ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to getfiles for %s: %s",
			     zif_package_get_id (item->package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* add files to the provides */
	for (i=0; i<files->len; i++) {
		filename = g_ptr_array_index (files, i);
		depend = zif_depend_new ();
		zif_depend_set_name (depend, filename);
		zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_ANY);
		g_ptr_array_add (provides, depend);
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
	if (files != NULL)
		g_ptr_array_unref (files);
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
	guint i;
	ZifDepend *depend;
	ZifPackage *package;

	/* does anything obsolete this package */
	depend = zif_depend_new ();
	zif_depend_set_name (depend, zif_package_get_name (item->package));
	zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_version (depend, zif_package_get_version (item->package));

	/* search the remote stores */
	zif_state_reset (data->state);
	obsoletes = zif_store_array_what_obsoletes (data->transaction->priv->stores_remote,
						    depend,
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
							   item->package,
							   ZIF_TRANSACTION_REASON_OBSOLETE,
							   error);
		if (!ret)
			goto out;

		/* is already installed */
		if (zif_transaction_get_item_from_array (data->transaction->priv->install,
							 package) != NULL)
			goto success;

		/* add the new package */
		ret = zif_transaction_add_install_internal (data->transaction,
							    package,
							    item->package,
							    item->reason,
							    error);
		if (!ret)
			goto out;

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
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "installed package %s is newer than package updated %s",
			     zif_package_get_id (package),
			     zif_package_get_id (item->package));
		goto out;
	}

	/* remove the installed package */
	ret = zif_transaction_add_remove_internal (data->transaction,
						   item->package,
						   item->package,
						   ZIF_TRANSACTION_REASON_UPDATE,
						   error);
	if (!ret)
		goto out;

	/* add the new package */
	ret = zif_transaction_add_install_internal (data->transaction,
						    package,
						    item->package,
						    ZIF_TRANSACTION_REASON_UPDATE,
						    error);
	if (!ret)
		goto out;

success:

	/* new things to process */
	data->unresolved_dependencies = TRUE;

	/* mark as resolved, so we don't try to process this again */
	item->resolved = TRUE;
out:
	if (obsoletes != NULL)
		g_ptr_array_unref (obsoletes);
	if (depend != NULL)
		g_object_unref (depend);
	if (package != NULL)
		g_object_unref (package);
	return ret;
}

/**
 * zif_transaction_package_conflicts:
 * @satisfies: the matched depend, free with g_object_unref() in for not %NULL
 **/
static gboolean
zif_transaction_package_conflicts (ZifPackage *package,
				   ZifDepend *depend,
				   ZifDepend **satisfies,
				   ZifState *state,
				   GError **error)
{
	const gchar *depend_description;
	gboolean ret = TRUE;
	GError *error_local = NULL;
	GPtrArray *conflicts = NULL;
	guint i;
	ZifDepend *depend_tmp;

	/* get the list of conflicts (which is cached after the first access) */
	zif_state_reset (state);
	conflicts = zif_package_get_conflicts (package, state, &error_local);
	if (conflicts == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get conflicts for %s: %s",
			     zif_package_get_id (package),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* find what we're looking for */
	depend_description = zif_depend_get_description (depend);
	for (i=0; i<conflicts->len; i++) {
		depend_tmp = g_ptr_array_index (conflicts, i);
		ret = zif_depend_satisfies (depend, depend_tmp);
		if (ret) {
			*satisfies = g_object_ref (depend_tmp);
			goto out;
		}
	}

	/* success, but did not find */
	ret = TRUE;
	*satisfies = NULL;
out:
	if (conflicts != NULL)
		g_ptr_array_unref (conflicts);
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
	guint i;
	ZifPackage *package_tmp;
	GPtrArray *satisfy_array;
	ZifDepend *satisfies = NULL;
	GError *error_local = NULL;

	/* interate through the array */
	satisfy_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array->len; i++) {
		package_tmp = g_ptr_array_index (array, i);

		/* does this match */
		ret = zif_transaction_package_conflicts (package_tmp, depend, &satisfies, state, error);
		if (!ret)
			goto out;

		/* gotcha, but keep looking */
		if (satisfies != NULL) {
			g_ptr_array_add (satisfy_array, g_object_ref (package_tmp));
			g_object_unref (satisfies);
		}
	}

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
 * zif_transaction_get_package_conflict_from_array:
 **/
static gboolean
zif_transaction_get_package_conflict_from_array (GPtrArray *array,
						 ZifDepend *depend,
						 ZifPackage **package,
						 ZifState *state,
						 GError **error)
{
	gboolean ret = TRUE;
	guint i;
	ZifTransactionItem *item;
	GPtrArray *satisfy_array;
	ZifDepend *satisfies = NULL;
	GError *error_local = NULL;

	/* interate through the array */
	satisfy_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* does this match */
		ret = zif_transaction_package_conflicts (item->package, depend, &satisfies, state, error);
		if (!ret)
			goto out;

		/* gotcha, but keep looking */
		if (satisfies != NULL) {
			g_ptr_array_add (satisfy_array, g_object_ref (item->package));
			g_object_unref (satisfies);
		}
	}

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
 * zif_transaction_get_package_conflict_from_store:
 *
 * Returns a package that conflicts something.
 **/
static gboolean
zif_transaction_get_package_conflict_from_store (ZifStore *store,
						 ZifDepend *depend,
						 ZifPackage **package,
						 ZifState *state,
						 GError **error)
{
	GPtrArray *array;
	GError *error_local = NULL;
	gboolean ret = FALSE;

	/* get the package list */
	zif_state_reset (state);
	array = zif_store_get_packages (store, state, &error_local);
	if (array == NULL) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to get installed package list: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* search it */
	ret = zif_transaction_get_package_conflict_from_package_array (array,
								       depend,
								       package,
								       state,
								       error);
	if (!ret) {
		g_assert (error == NULL || *error != NULL);
		goto out;
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
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
//	ZifPackage *package;
	ZifPackage *conflicting;
	ZifDepend *depend;
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

	g_debug ("checking %i provides for %s",
		 provides->len,
		 zif_package_get_id (item->package));
	for (i=0; i<provides->len; i++) {

		depend = g_ptr_array_index (provides, i);
		g_debug ("checking provide %s",
			 zif_depend_get_description (depend));

		/* get packages that conflict with this */
		ret = zif_transaction_get_package_conflict_from_store (data->transaction->priv->store_local,
								       depend, &conflicting,
								       data->state, error);
		if (!ret) {
			g_assert (error == NULL || *error != NULL);
			goto out;
		}

		/* get packages that conflict with this in the install array */
		if (conflicting == NULL) {
			ret = zif_transaction_get_package_conflict_from_array (data->transaction->priv->install,
									       depend, &conflicting,
									       data->state, error);
			if (!ret) {
				g_assert (error == NULL || *error != NULL);
				goto out;
			}
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
		ret = zif_transaction_get_package_provide_from_store (data->transaction->priv->store_local,
								      depend, &conflicting,
								      data->state, error);
		if (!ret) {
			g_assert (error == NULL || *error != NULL);
			goto out;
		}

		/* does this install conflict with another package in install array */
		if (conflicting == NULL) {
			ret = zif_transaction_get_package_provide_from_array (data->transaction->priv->install,
									      depend, &conflicting,
									      data->state, error);
			if (!ret) {
				g_assert (error == NULL || *error != NULL);
				goto out;
			}
		}

		/* we conflict with something */
		if (conflicting != NULL) {
			ret = FALSE;
			g_set_error (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_CONFLICTING,
				     "%s conflicts with %s",
				     zif_package_get_id (item->package),
				     zif_package_get_id (conflicting));
			g_object_unref (conflicting);
			goto out;
		}
	}
out:
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
	item_tmp = zif_transaction_get_item_from_array_by_update_package (transaction->priv->install,
									  package);
	if (item_tmp != NULL) {
		g_debug ("REMOVE %s as CANCELLED",
			 zif_package_get_id (item_tmp->package));
		g_ptr_array_remove (transaction->priv->install, item_tmp);
	}

	/* remove the thing we just added to remove queue too */
	item_tmp = zif_transaction_get_item_from_array_by_update_package (transaction->priv->remove,
									  package);
	if (item_tmp != NULL) {
		g_debug ("REMOVE %s as CANCELLED",
			 zif_package_get_id (item_tmp->package));
		g_ptr_array_remove (transaction->priv->remove, item_tmp);
	}
}

/**
 * zif_transaction_resolve_wind_back_failure:
 **/
static void
zif_transaction_resolve_wind_back_failure (ZifTransaction *transaction, ZifTransactionItem *item)
{
	/* remove the things we just added to the install queue too */
	zif_transaction_resolve_wind_back_failure_package (transaction,
							   item->package);
	if (item->update_package != NULL)
		zif_transaction_resolve_wind_back_failure_package (transaction,
								   item->update_package);
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
	guint i;
	gboolean ret = FALSE;
	ZifTransactionItem *item;
	ZifTransactionResolve *data = NULL;
	GError *error_local = NULL;
	guint resolve_count = 0;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (transaction->priv->stores_remote != NULL, FALSE);
	g_return_val_if_fail (transaction->priv->store_local != NULL, FALSE);

	g_debug ("starting resolve with %i to install, %i to update, and %i to remove",
		 transaction->priv->install->len,
		 transaction->priv->update->len,
		 transaction->priv->remove->len);

	/* whilst there are unresolved dependencies, keep trying */
	data = g_new0 (ZifTransactionResolve, 1);
	data->state = state;
	data->transaction = transaction;
	data->unresolved_dependencies = TRUE;
	while (data->unresolved_dependencies) {

		/* reset here */
		resolve_count++;
		data->unresolved_dependencies = FALSE;

		/* for each package set to be installed */
		g_debug ("starting INSTALL on loop %i", resolve_count);
		for (i=0; i<transaction->priv->install->len; i++) {
			item = g_ptr_array_index (transaction->priv->install, i);
			if (item->resolved)
				continue;

			/* resolve this item */
			ret = zif_transaction_resolve_install_item (data, item, &error_local);
			if (!ret) {
				g_assert (error_local != NULL);
				/* special error code */
				if (error_local->code == ZIF_TRANSACTION_ERROR_NOTHING_TO_DO) {
					g_debug ("REMOVE %s as nothing to do",
						 zif_package_get_id (item->package));
					g_ptr_array_remove (transaction->priv->install, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				if (transaction->priv->skip_broken) {
					g_debug ("ignoring error as we're skip-broken: %s",
						 error_local->message);
					zif_transaction_resolve_wind_back_failure (transaction, item);
					g_ptr_array_remove (transaction->priv->install, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				g_propagate_error (error, error_local);
				goto out;
			}
		}

		/* for each package set to be updated */
		g_debug ("starting UPDATE on loop %i", resolve_count);
		for (i=0; i<transaction->priv->update->len; i++) {
			item = g_ptr_array_index (transaction->priv->update, i);
			if (item->resolved)
				continue;

			/* resolve this item */
			ret = zif_transaction_resolve_update_item (data, item, &error_local);
			if (!ret) {
				g_assert (error_local != NULL);
				/* special error code */
				if (error_local->code == ZIF_TRANSACTION_ERROR_NOTHING_TO_DO) {
					g_debug ("REMOVE %s as nothing to do",
						 zif_package_get_id (item->package));
					g_ptr_array_remove (transaction->priv->update, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				if (transaction->priv->skip_broken) {
					g_debug ("ignoring error as we're skip-broken: %s",
						 error_local->message);
					zif_transaction_resolve_wind_back_failure (transaction, item);
					g_ptr_array_remove (transaction->priv->update, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				g_propagate_error (error, error_local);
				goto out;
			}
		}

		/* for each package set to be removed */
		g_debug ("starting REMOVE on loop %i", resolve_count);
		for (i=0; i<transaction->priv->remove->len; i++) {
			item = g_ptr_array_index (transaction->priv->remove, i);
			if (item->resolved)
				continue;

			/* resolve this item */
			ret = zif_transaction_resolve_remove_item (data, item, &error_local);
			if (!ret) {
				g_assert (error_local != NULL);
				/* special error code */
				if (error_local->code == ZIF_TRANSACTION_ERROR_NOTHING_TO_DO) {
					g_debug ("REMOVE %s as nothing to do",
						 zif_package_get_id (item->package));
					g_ptr_array_remove (transaction->priv->remove, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				if (transaction->priv->skip_broken) {
					g_debug ("ignoring error as we're skip-broken: %s",
						 error_local->message);
					zif_transaction_resolve_wind_back_failure (transaction, item);
					g_ptr_array_remove (transaction->priv->remove, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				g_propagate_error (error, error_local);
				goto out;
			}
		}

		/* check conflicts */
		g_debug ("starting CONFLICTS on loop %i", resolve_count);
		for (i=0; i<transaction->priv->install->len; i++) {
			item = g_ptr_array_index (transaction->priv->install, i);

			/* check this item */
			ret = zif_transaction_resolve_conflicts_item (data, item, &error_local);
			if (!ret) {
				if (transaction->priv->skip_broken) {
					g_debug ("ignoring error as we're skip-broken: %s",
						 error_local->message);
					g_ptr_array_remove (transaction->priv->install, item);
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
	if (transaction->priv->install->len == 0 &&
	    transaction->priv->update->len == 0 &&
	    transaction->priv->remove->len == 0) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_TRANSACTION_ERROR,
				     ZIF_TRANSACTION_ERROR_NOTHING_TO_DO,
				     "no packages will be installed, removed or updated");
		goto out;
	}

	/* success */
	transaction->priv->state = ZIF_TRANSACTION_STATE_RESOLVED;
	g_debug ("done depsolve");
	zif_transaction_show_array ("installing", transaction->priv->install);
	zif_transaction_show_array ("removing", transaction->priv->remove);
	ret = TRUE;
out:
	g_free (data);
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

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* is valid */
	if (transaction->priv->state != ZIF_TRANSACTION_STATE_RESOLVED) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "not in resolve state, instead is %s",
			     zif_transaction_state_to_string (transaction->priv->state));
		goto out;
	}

	//FIXME
	g_set_error (error,
		     ZIF_TRANSACTION_ERROR,
		     ZIF_TRANSACTION_ERROR_NOT_SUPPORTED,
		     "not yet supported");
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
	gboolean ret = FALSE;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* is valid */
	if (transaction->priv->state != ZIF_TRANSACTION_STATE_PREPARED) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "not in prepared state, instead is %s",
			     zif_transaction_state_to_string (transaction->priv->state));
		goto out;
	}

	//FIXME
	g_set_error (error,
		     ZIF_TRANSACTION_ERROR,
		     ZIF_TRANSACTION_ERROR_NOT_SUPPORTED,
		     "not yet supported");
out:
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
 * zif_transaction_set_skip_broken:
 * @transaction: the #ZifTransaction object
 * @skip_broken: if the resolve should skip broken packages
 *
 * Sets the skip broken policy for resolving. If this is TRUE, then
 * checking zif_transaction_get_unresolved() after resolving would
 * be a good idea.
 *
 * Since: 0.1.3
 **/
void
zif_transaction_set_skip_broken (ZifTransaction *transaction, gboolean skip_broken)
{
	g_return_if_fail (ZIF_IS_TRANSACTION (transaction));
	transaction->priv->skip_broken = skip_broken;
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
 * zif_transaction_get_reason:
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

	g_ptr_array_unref (transaction->priv->install);
	g_ptr_array_unref (transaction->priv->update);
	g_ptr_array_unref (transaction->priv->remove);
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

	/* packages we want to install */
	transaction->priv->install = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_transaction_item_free);

	/* packages we want to update */
	transaction->priv->update = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_transaction_item_free);

	/* packages we want to remove */
	transaction->priv->remove = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_transaction_item_free);;
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
