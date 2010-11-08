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

#define ZIF_TRANSACTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_TRANSACTION, ZifTransactionPrivate))

struct _ZifTransactionPrivate
{
	GHashTable		*install;
	GHashTable		*remove;
};

G_DEFINE_TYPE (ZifTransaction, zif_transaction, G_TYPE_OBJECT)

typedef struct {
	ZifPackage		*package;
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
 * zif_transaction_item_free:
 **/
static void
zif_transaction_item_free (ZifTransactionItem *item)
{
	g_object_unref (item->package);
	g_free (item);
}

/**
 * zif_transaction_add_to_hash:
 **/
static gboolean
zif_transaction_add_to_hash (GHashTable *hash, ZifPackage *package,
			     ZifPackage *related_package)
{
	gboolean ret = FALSE;
	const gchar *package_id;
	ZifTransactionItem *item;

	/* already added? */
	package_id = zif_package_get_id (package);
	item = g_hash_table_lookup (hash, package_id);
	if (item != NULL)
		goto out;

	/* create new item */
	item = g_new0 (ZifTransactionItem, 1);
	item->package = g_object_ref (package);
	g_hash_table_insert (hash, g_strdup (package_id), item);

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
	ret = zif_transaction_add_to_hash (transaction->priv->install, package, NULL);
	if (!ret) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Not adding already added package for install %s",
			     zif_package_get_id (package));
		goto out;
	}

	g_debug ("Added for install %s", zif_package_get_id (package));
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
	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_set_error_literal (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "not supported");
	return FALSE;
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
	ret = zif_transaction_add_to_hash (transaction->priv->remove, package, NULL);
	if (!ret) {
		g_set_error (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "Not adding already added package for remove %s",
			     zif_package_get_id (package));
		goto out;
	}

	g_debug ("Added for remove %s", zif_package_get_id (package));
out:
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
	g_return_val_if_fail (ZIF_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_set_error_literal (error,
			     ZIF_TRANSACTION_ERROR,
			     ZIF_TRANSACTION_ERROR_FAILED,
			     "not supported");
	return FALSE;
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

	g_hash_table_destroy (transaction->priv->install);
	g_hash_table_destroy (transaction->priv->remove);

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
	transaction->priv->install = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
						(GDestroyNotify) zif_transaction_item_free);

	/* packages we want to remove */
	transaction->priv->remove = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
						(GDestroyNotify) zif_transaction_item_free);
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
