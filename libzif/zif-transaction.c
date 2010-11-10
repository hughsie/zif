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
};

typedef struct {
	ZifState		*state;
	ZifTransaction		*transaction;
	gboolean		 unresolved_dependencies;
} ZifTransactionResolve;

G_DEFINE_TYPE (ZifTransaction, zif_transaction, G_TYPE_OBJECT)

typedef struct {
	ZifPackage		*package;
	gboolean		 resolved;
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
 * zif_transaction_add_to_array:
 **/
static gboolean
zif_transaction_add_to_array (GPtrArray *array, ZifPackage *package,
			      ZifPackage *related_package)
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
	item->package = g_object_ref (package);
	g_ptr_array_add (array, item);

	/* success */
	ret = TRUE;
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
zif_transaction_add_install (ZifTransaction *transaction, ZifPackage *package, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* add to install */
	ret = zif_transaction_add_to_array (transaction->priv->install, package, NULL);
	if (!ret) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Not adding already added package for install %s",
			     zif_package_get_id (package));
		goto out;
	}

	g_debug ("Add INSTALL %s", zif_package_get_id (package));
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
	ret = zif_transaction_add_to_array (transaction->priv->update, package, NULL);
	if (!ret) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Not adding already added package for update %s",
			     zif_package_get_id (package));
		goto out;
	}

	g_debug ("Add UPDATE %s", zif_package_get_id (package));
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
	ret = zif_transaction_add_to_array (transaction->priv->remove, package, NULL);
	if (!ret) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Not adding already added package for remove %s",
			     zif_package_get_id (package));
		goto out;
	}

	g_debug ("Add REMOVE %s", zif_package_get_id (package));
out:
	return ret;
}

/**
 * zif_transaction_package_file_depend:
 **/
static gboolean
zif_transaction_package_file_depend (ZifPackage *package,
				     const gchar *filename,
				     gboolean *satisfies,
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
	g_debug ("%i files for %s",
		 provides->len,
		 zif_package_get_id (package));
	for (i=0; i<provides->len; i++) {
		filename_tmp = g_ptr_array_index (provides, i);
		g_debug ("require: %s:%s", filename_tmp, filename);
		if (g_strcmp0 (filename_tmp, filename) == 0) {
			*satisfies = TRUE;
			goto out;
		}
	}

	/* success, but did not find */
	*satisfies = FALSE;
out:
	return ret;
}

/**
 * zif_transaction_package_provides:
 **/
static gboolean
zif_transaction_package_provides (ZifPackage *package,
				  ZifDepend *depend,
				  gboolean *satisfies,
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
			*satisfies = TRUE;
			goto out;
		}
	}

	/* success, but did not find */
	ret = TRUE;
	*satisfies = FALSE;
out:
	if (provides != NULL)
		g_ptr_array_unref (provides);
	return ret;
}

/**
 * zif_transaction_package_requires:
 *
 * Satisfies is %TRUE if the dep is satisfied by package
 **/
static gboolean
zif_transaction_package_requires (ZifPackage *package,
				  ZifDepend *depend,
				  gboolean *satisfies,
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
	g_debug ("Find out if %s requires %s",
		 zif_package_get_id (package),
		 zif_depend_get_description (depend));
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
	g_debug ("got %i requires for %s",
		 requires->len,
		 zif_package_get_id (package));
	for (i=0; i<requires->len; i++) {
		depend_tmp = g_ptr_array_index (requires, i);
		g_debug ("%i.\t%s",
			 i+1,
			 zif_depend_get_description (depend_tmp));
	}
	depend_description = zif_depend_get_description (depend);
	for (i=0; i<requires->len; i++) {
		depend_tmp = g_ptr_array_index (requires, i);
		g_debug ("require: %s:%s", depend_description, zif_depend_get_description (depend_tmp));
		if (zif_depend_satisfies (depend, depend_tmp)) {
			g_debug ("%s satisfied by %s",
				 zif_depend_get_description (depend_tmp),
				 zif_package_get_id (package));
			ret = TRUE;
			*satisfies = TRUE;
			goto out;
		}
	}

	/* success, but did not find */
	ret = TRUE;
	*satisfies = FALSE;
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
	gboolean satisfies = FALSE;

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
		if (satisfies) {
			*package = g_object_ref (item->package);
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
 * _zif_package_array_get_packages_with_best_arch:
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
static GPtrArray *
_zif_package_array_get_packages_with_best_arch (GPtrArray *array)
{
	GPtrArray *results;
	ZifPackage *package;
	guint i, j;
	gboolean added_something = FALSE;
	const gchar *arch_priorities[] = { "i686", "i586", "i386", NULL };

	results = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_ref);

	/* search for arch priorities in the correct order */
	//FIXME, get the preferred arch list for 64bit
	for (j=0; arch_priorities[j] != NULL; j++) {
		for (i=0; i<array->len; i++) {
			package = g_ptr_array_index (array, i);
			if (g_strcmp0 (zif_package_get_arch (package), arch_priorities[j]) == 0) {
				g_ptr_array_add (results, g_object_ref (package));
				added_something = TRUE;
			}
		}
		if (added_something)
			break;
	}

	return results;
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
	GPtrArray *satisfy_arch_array = NULL;
	gboolean satisfies = FALSE;
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
		if (satisfies)
			g_ptr_array_add (satisfy_array, g_object_ref (package_tmp));
	}

	/* success, but no results */
	if (satisfy_array->len == 0) {
		*package = NULL;
		goto out;
	}

	/* optimize for one single result */
	if (satisfy_array->len == 1) {
		*package = g_object_ref (g_ptr_array_index (satisfy_array, 0));
		goto out;
	}

	g_debug ("provide %s still has %i matches, filtering down to best architecture",
		 zif_depend_get_description (depend),
		 satisfy_array->len);

	/* filter these down so we get best architectures listed first */
	satisfy_arch_array = _zif_package_array_get_packages_with_best_arch (satisfy_array);

	/* optimize for one single result */
	if (satisfy_arch_array->len == 1) {
		*package = g_object_ref (g_ptr_array_index (satisfy_arch_array, 0));
		goto out;
	}

	/* return the newest */
	g_debug ("provide %s still has %i matches, choosing newest",
		 zif_depend_get_description (depend),
		 satisfy_array->len);
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
	if (satisfy_arch_array != NULL)
		g_ptr_array_unref (satisfy_arch_array);
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
						 GPtrArray **requires,
						 ZifState *state,
						 GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	GPtrArray *array;
	guint i;
	ZifPackage *package;
	gboolean satisfies = FALSE;

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
		if (satisfies) {
			g_debug ("adding %s to requires", zif_package_get_id (package));
			g_ptr_array_add (*requires, g_object_ref (package));
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

	/* already provided in the rpmdb */
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
			ret = zif_transaction_add_remove (data->transaction, package, error);
			if (!ret)
				goto out;
		}
skip_resolve:
		/* add the provide to the install set */
		zif_transaction_add_install (data->transaction, package_provide, NULL);
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
			ret = zif_transaction_add_remove (data->transaction,
							  package_oldest, error);
			if (!ret) {
				g_assert (error == NULL || *error != NULL);
				goto out;
			}
		}
	}

skip_resolve:

	g_debug ("getting requires for %s", zif_package_get_id (item->package));
	zif_state_reset (data->state); //FIXME
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
					ZifPackage *source,
					ZifDepend *depend,
					GError **error)
{
	gboolean ret = TRUE;
	guint i;
	GPtrArray *package_requires = NULL;
	ZifPackage *package;

	/* find if anything in the local store requires this package */
	g_debug ("find anything installed that requires %s provided by %s",
		 zif_depend_get_description (depend),
		 zif_package_get_id (source));
	ret = zif_transaction_get_package_requires_from_store (data->transaction->priv->store_local,
							       depend, &package_requires,
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
	g_debug ("%i packages require %s provided by %s",
		 package_requires->len,
		 zif_depend_get_description (depend),
		 zif_package_get_id (source));
	for (i=0; i<package_requires->len; i++) {
		package = g_ptr_array_index (package_requires, i);
		g_debug ("%i.\t%s", i+1, zif_package_get_id (package));
	}
	for (i=0; i<package_requires->len; i++) {
		package = g_ptr_array_index (package_requires, i);

		/* don't remove ourself */
		if (source == package)
			continue;

		/* remove this too */
		g_debug ("depend %s is required by %s (installed), so remove",
			 zif_depend_get_description (depend),
			 zif_package_get_id (package));

		/* add the provide to the install set */
		ret = zif_transaction_add_remove (data->transaction, package, error);
		if (!ret)
			goto out;
		data->unresolved_dependencies = TRUE;
	}
out:
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
//	ZifDepend *virtual_depend = NULL;
	const gchar *filename;

	/* make a list of anything this package provides */
	g_debug ("getting provides for %s", zif_package_get_id (item->package));
	zif_state_reset (data->state); //FIXME
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
	zif_state_reset (data->state); //FIXME
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


#if 0
	/* get rid of the virtual provide that we added manually */
	virtual_depend = zif_depend_new ();
	zif_depend_set_flag (virtual_depend, ZIF_DEPEND_FLAG_EQUAL);
	zif_depend_set_name (virtual_depend, zif_package_get_name (item->package));
	zif_depend_set_version (virtual_depend, zif_package_get_version (item->package));
	for (i=0; i<provides->len; i++) {
		depend = g_ptr_array_index (provides, i);
		if (zif_depend_satisfies (depend, virtual_depend)) {
			g_debug ("removing virtual provide %s",
				 zif_depend_get_description (depend));
			g_ptr_array_remove_index_fast (provides, i);
			break;
		}
	}
	g_object_unref (virtual_depend);
#endif

	/* find each provide */
	g_debug ("got %i provides", provides->len);
	for (i=0; i<provides->len; i++) {
		depend = g_ptr_array_index (provides, i);
		g_debug ("%i.\t%s", i+1, zif_depend_get_description (depend));
	}
	for (i=0; i<provides->len; i++) {
		depend = g_ptr_array_index (provides, i);
		ret = zif_transaction_resolve_remove_require (data, item->package, depend, error);
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
		g_debug ("%i.\t%s", i+1, zif_package_get_id (item->package));
	}
}

/**
 * zif_transaction_get_newest_from_remote_by_names:
 **/
static ZifPackage *
zif_transaction_get_newest_from_remote_by_names (ZifTransactionResolve *data, const gchar *name, GError **error)
{
	GPtrArray *matches;
	ZifPackage *package = NULL;
	const gchar *search[] = { NULL, NULL };

	/* get resolve in the array */
	search[0] = name;
	zif_state_reset (data->state);
	matches = zif_store_array_resolve (data->transaction->priv->stores_remote,
					   (gchar **) search, data->state, error);
	if (matches == NULL)
		goto out;

	/* we found nothing */
	if (matches->len == 0) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "cannot find installed %s", name);
		goto out;
	}

	/* get the newest package */
	zif_package_array_filter_newest (matches);
	package = g_object_ref (g_ptr_array_index (matches, 0));
out:
	g_ptr_array_unref (matches);
	return package;
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
	gint value;
	ZifPackage *package;
	GError *error_local = NULL;

	/* get the newest package available from the remote stores */
	package = zif_transaction_get_newest_from_remote_by_names (data,
								   zif_package_get_name (item->package),
								   &error_local);
	if (package == NULL) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "failed to find %s in installed store: %s",
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
	ret = zif_transaction_add_remove (data->transaction, item->package, error);
	if (!ret)
		goto out;

	/* add the new package */
	ret = zif_transaction_add_install (data->transaction, package, error);
	if (!ret)
		goto out;

	/* new things to process */
	data->unresolved_dependencies = TRUE;

	/* mark as resolved, so we don't try to process this again */
	item->resolved = TRUE;
out:
	if (package != NULL)
		g_object_unref (package);
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
	guint i;
	gboolean ret = FALSE;
	ZifTransactionItem *item;
	ZifTransactionResolve *data;
	GError *error_local = NULL;
	guint resolve_count = 0;

	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (transaction->priv->stores_remote != NULL, FALSE);
	g_return_val_if_fail (transaction->priv->store_local != NULL, FALSE);

	g_debug ("starting resolve");

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
					g_ptr_array_remove (transaction->priv->remove, item);
					data->unresolved_dependencies = TRUE;
					g_clear_error (&error_local);
					break;
				}
				g_propagate_error (error, error_local);
				goto out;
			}
		}
	}

	/* success */
	g_debug ("done depsolve");
	zif_transaction_show_array ("installing", transaction->priv->install);
//	zif_transaction_show_array ("updating", transaction->priv->update);
	zif_transaction_show_array ("removing", transaction->priv->remove);
	ret = TRUE;
out:
	g_free (data);
	return ret;
}

/**
 * zif_transaction_set_store_local:
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
