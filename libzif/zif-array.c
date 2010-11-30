/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-array
 * @short_description: Hashed array Implimentation
 *
 * A #ZifArray can be treated either as a hash table, or an array.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-array.h"

#define ZIF_ARRAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_ARRAY, ZifArrayPrivate))

struct _ZifArrayPrivate
{
	GPtrArray		*array;
	GHashTable		*hash;
	ZifArrayMappingFuncCb	 mapping_func;
};

G_DEFINE_TYPE (ZifArray, zif_array, G_TYPE_OBJECT)

/**
 * zif_array_add:
 * @array: A #ZifArray
 * @data: The object to store in the array
 *
 * Adds an object to the array.
 * The object is refcounted internally.
 *
 * Return value: %TRUE if the object was added
 *
 * Since: 0.1.3
 **/
gboolean
zif_array_add (ZifArray *array, gpointer data)
{
	gboolean ret = TRUE;
	GObject *object_tmp;
	const gchar *key;
	GObject *object = G_OBJECT (data);

	g_return_val_if_fail (ZIF_IS_ARRAY (array), FALSE);
	g_return_val_if_fail (array->priv->mapping_func != NULL, FALSE);

	/* check */
	key = array->priv->mapping_func (object);
	object_tmp = zif_array_lookup_with_key (array, key);
	if (object_tmp != NULL) {
		ret = FALSE;
		goto out;
	}

	/* add */
	g_ptr_array_add (array->priv->array, g_object_ref (object));
	g_hash_table_insert (array->priv->hash,
			     g_strdup (key),
			     object);

	/* shadow copy */
	array->len = array->priv->array->len;
out:
	return ret;
}

/**
 * zif_array_remove:
 * @array: A #ZifArray
 * @data: The object to remove
 *
 * Removes an object from the array.
 *
 * Return value: %TRUE if the object was removed
 *
 * Since: 0.1.3
 **/
gboolean
zif_array_remove (ZifArray *array, gpointer data)
{
	gboolean ret = TRUE;
	GObject *object_tmp;
	const gchar *key;
	GObject *object = G_OBJECT (data);

	g_return_val_if_fail (ZIF_IS_ARRAY (array), FALSE);
	g_return_val_if_fail (array->priv->mapping_func != NULL, FALSE);

	/* check */
	key = array->priv->mapping_func (object);
	object_tmp = zif_array_lookup_with_key (array, key);
	if (object_tmp == NULL) {
		ret = FALSE;
		goto out;
	}

	/* remove */
	g_ptr_array_remove (array->priv->array, object_tmp);
	g_hash_table_remove (array->priv->hash, key);

	/* shadow copy */
	array->len = array->priv->array->len;
out:
	return ret;
}

/**
 * zif_array_remove_with_key:
 * @array: A #ZifArray
 * @key: The object key to remove
 *
 * Removes an object from the array.
 *
 * Return value: %TRUE if the object was removed
 *
 * Since: 0.1.3
 **/
gboolean
zif_array_remove_with_key (ZifArray *array, const gchar *key)
{
	gboolean ret = TRUE;
	GObject *object_tmp;

	g_return_val_if_fail (ZIF_IS_ARRAY (array), FALSE);
	g_return_val_if_fail (array->priv->mapping_func != NULL, FALSE);

	/* check */
	object_tmp = zif_array_lookup_with_key (array, key);
	if (object_tmp == NULL) {
		ret = FALSE;
		goto out;
	}

	/* remove */
	g_ptr_array_remove (array->priv->array, object_tmp);
	g_hash_table_remove (array->priv->hash, object_tmp);

	/* shadow copy */
	array->len = array->priv->array->len;
out:
	return ret;
}

/**
 * zif_array_lookup:
 * @array: A #ZifArray
 * @data: The object to find
 *
 * Looks up an object from the array.
 *
 * Return value: An object, which is *not* ref'd.
 *
 * Since: 0.1.3
 **/
GObject *
zif_array_lookup (ZifArray *array, gpointer data)
{
	GObject *object = G_OBJECT (data);

	g_return_val_if_fail (ZIF_IS_ARRAY (array), NULL);
	g_return_val_if_fail (array->priv->mapping_func != NULL, NULL);

	return g_hash_table_lookup (array->priv->hash,
				    array->priv->mapping_func (object));
}

/**
 * zif_array_lookup_with_key:
 * @array: A #ZifArray
 * @key: The object key to find
 *
 * Looks up an object from the array.
 *
 * Return value: An object, which is *not* ref'd.
 *
 * Since: 0.1.3
 **/
GObject *
zif_array_lookup_with_key (ZifArray *array, const gchar *key)
{
	g_return_val_if_fail (ZIF_IS_ARRAY (array), NULL);
	g_return_val_if_fail (array->priv->mapping_func != NULL, NULL);

	return g_hash_table_lookup (array->priv->hash, key);
}

/**
 * zif_array_index:
 * @array: A #ZifArray
 * @index: The array index
 *
 * Gets an object from the array.
 *
 * Return value: An object, which is *not* ref'd.
 *
 * Since: 0.1.3
 **/
GObject *
zif_array_index (ZifArray *array, guint index)
{
	g_return_val_if_fail (ZIF_IS_ARRAY (array), NULL);
	g_return_val_if_fail (array->priv->mapping_func != NULL, NULL);
	return g_ptr_array_index (array->priv->array, index);
}

/**
 * zif_array_get_array:
 * @array: A #ZifArray
 *
 * Gets the object array
 *
 * Return value: The refd #GPtrArray.
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_array_get_array (ZifArray *array)
{
	g_return_val_if_fail (ZIF_IS_ARRAY (array), NULL);
	return g_ptr_array_ref (array->priv->array);
}

/**
 * zif_array_set_mapping_func:
 * @array: A #ZifArray
 * @mapping_func: A mapping function from GObject to const string.
 *
 * Sets the mapping function.
 *
 * Since: 0.1.3
 **/
void
zif_array_set_mapping_func (ZifArray *array, ZifArrayMappingFuncCb mapping_func)
{
	g_return_if_fail (ZIF_IS_ARRAY (array));
	array->priv->mapping_func = mapping_func;
}

/**
 * zif_array_finalize:
 **/
static void
zif_array_finalize (GObject *object)
{
	ZifArray *array;
	g_return_if_fail (ZIF_IS_ARRAY (object));
	array = ZIF_ARRAY (object);

	g_ptr_array_unref (array->priv->array);
	g_hash_table_destroy (array->priv->hash);

	G_OBJECT_CLASS (zif_array_parent_class)->finalize (object);
}

/**
 * zif_array_class_init:
 **/
static void
zif_array_class_init (ZifArrayClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_array_finalize;
	g_type_class_add_private (klass, sizeof (ZifArrayPrivate));
}

/**
 * zif_array_init:
 **/
static void
zif_array_init (ZifArray *array)
{
	array->priv = ZIF_ARRAY_GET_PRIVATE (array);
	array->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	array->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * zif_array_new:
 *
 * Return value: A new #ZifArray instance.
 *
 * Since: 0.1.3
 **/
ZifArray *
zif_array_new (void)
{
	ZifArray *array;
	array = g_object_new (ZIF_TYPE_ARRAY, NULL);
	return ZIF_ARRAY (array);
}

