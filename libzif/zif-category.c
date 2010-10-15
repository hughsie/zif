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
 * SECTION:zif-category
 * @short_description: Category object
 *
 * This GObject represents a category in the group system.
 */

#include "config.h"

#include <glib-object.h>

#include "zif-category.h"

static void     zif_category_finalize	(GObject     *object);

#define ZIF_CATEGORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_CATEGORY, ZifCategoryPrivate))

/**
 * ZifCategoryPrivate:
 *
 * Private #ZifCategory data
 **/
struct _ZifCategoryPrivate
{
	gchar				*parent_id;
	gchar				*cat_id;
	gchar				*name;
	gchar				*summary;
	gchar				*icon;
};

enum {
	PROP_0,
	PROP_PARENT_ID,
	PROP_CAT_ID,
	PROP_NAME,
	PROP_SUMMARY,
	PROP_ICON,
	PROP_LAST
};

G_DEFINE_TYPE (ZifCategory, zif_category, G_TYPE_OBJECT)

/**
 * zif_category_get_parent_id:
 * @category: The %ZifCategory
 *
 * Gets the parent category id.
 *
 * Return value: the string value, or %NULL for unset.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_category_get_parent_id (ZifCategory *category)
{
	g_return_val_if_fail (ZIF_IS_CATEGORY (category), NULL);
	return category->priv->parent_id;
}

/**
 * zif_category_set_parent_id:
 * @category: The %ZifCategory
 * @parent_id: the new value
 *
 * Sets the parent category id.
 *
 * Since: 0.1.0
 **/
void
zif_category_set_parent_id (ZifCategory *category, const gchar *parent_id)
{
	g_return_if_fail (ZIF_IS_CATEGORY (category));
	g_free (category->priv->parent_id);
	category->priv->parent_id = g_strdup (parent_id);
}

/**
 * zif_category_get_id:
 * @category: The %ZifCategory
 *
 * Gets the id specific to this category.
 *
 * Return value: the string value, or %NULL for unset.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_category_get_id (ZifCategory *category)
{
	g_return_val_if_fail (ZIF_IS_CATEGORY (category), NULL);
	return category->priv->cat_id;
}

/**
 * zif_category_set_id:
 * @category: The %ZifCategory
 * @cat_id: the new value
 *
 * Sets the id specific to this category.
 *
 * Since: 0.1.0
 **/
void
zif_category_set_id (ZifCategory *category, const gchar *cat_id)
{
	g_return_if_fail (ZIF_IS_CATEGORY (category));
	g_free (category->priv->cat_id);
	category->priv->cat_id = g_strdup (cat_id);
}

/**
 * zif_category_get_name:
 * @category: The %ZifCategory
 *
 * Gets the name.
 *
 * Return value: the string value, or %NULL for unset.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_category_get_name (ZifCategory *category)
{
	g_return_val_if_fail (ZIF_IS_CATEGORY (category), NULL);
	return category->priv->name;
}

/**
 * zif_category_set_name:
 * @category: The %ZifCategory
 * @name: the new value
 *
 * Sets the name.
 *
 * Since: 0.1.0
 **/
void
zif_category_set_name (ZifCategory *category, const gchar *name)
{
	g_return_if_fail (ZIF_IS_CATEGORY (category));
	g_free (category->priv->name);
	category->priv->name = g_strdup (name);
}

/**
 * zif_category_get_summary:
 * @category: The %ZifCategory
 *
 * Gets the summary.
 *
 * Return value: the string value, or %NULL for unset.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_category_get_summary (ZifCategory *category)
{
	g_return_val_if_fail (ZIF_IS_CATEGORY (category), NULL);
	return category->priv->summary;
}

/**
 * zif_category_set_summary:
 * @category: The %ZifCategory
 * @summary: the new value
 *
 * Sets the summary.
 *
 * Since: 0.1.0
 **/
void
zif_category_set_summary (ZifCategory *category, const gchar *summary)
{
	g_return_if_fail (ZIF_IS_CATEGORY (category));
	g_free (category->priv->summary);
	category->priv->summary = g_strdup (summary);
}

/**
 * zif_category_get_icon:
 * @category: The %ZifCategory
 *
 * Gets the icon filename.
 *
 * Return value: the string value, or %NULL for unset.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_category_get_icon (ZifCategory *category)
{
	g_return_val_if_fail (ZIF_IS_CATEGORY (category), NULL);
	return category->priv->icon;
}

/**
 * zif_category_set_icon:
 * @category: The %ZifCategory
 * @icon: the new value
 *
 * Sets the icon filename.
 *
 * Since: 0.1.0
 **/
void
zif_category_set_icon (ZifCategory *category, const gchar *icon)
{
	g_return_if_fail (ZIF_IS_CATEGORY (category));
	g_free (category->priv->icon);
	category->priv->icon = g_strdup (icon);
}

/**
 * zif_category_get_property:
 **/
static void
zif_category_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ZifCategory *category = ZIF_CATEGORY (object);
	ZifCategoryPrivate *priv = category->priv;

	switch (prop_id) {
	case PROP_PARENT_ID:
		g_value_set_string (value, priv->parent_id);
		break;
	case PROP_CAT_ID:
		g_value_set_string (value, priv->cat_id);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_SUMMARY:
		g_value_set_string (value, priv->summary);
		break;
	case PROP_ICON:
		g_value_set_string (value, priv->icon);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_category_set_property:
 **/
static void
zif_category_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	ZifCategory *category = ZIF_CATEGORY (object);
	ZifCategoryPrivate *priv = category->priv;

	switch (prop_id) {
	case PROP_PARENT_ID:
		g_free (priv->parent_id);
		priv->parent_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_CAT_ID:
		g_free (priv->cat_id);
		priv->cat_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_strdup (g_value_get_string (value));
		break;
	case PROP_SUMMARY:
		g_free (priv->summary);
		priv->summary = g_strdup (g_value_get_string (value));
		break;
	case PROP_ICON:
		g_free (priv->icon);
		priv->icon = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_category_class_init:
 **/
static void
zif_category_class_init (ZifCategoryClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_category_finalize;
	object_class->get_property = zif_category_get_property;
	object_class->set_property = zif_category_set_property;

	/**
	 * ZifCategory:parent-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("parent-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PARENT_ID, pspec);

	/**
	 * ZifCategory:cat-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("cat-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CAT_ID, pspec);

	/**
	 * ZifCategory:name:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_NAME, pspec);

	/**
	 * ZifCategory:summary:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("summary", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);

	/**
	 * ZifCategory:icon:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("icon", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ICON, pspec);

	g_type_class_add_private (klass, sizeof (ZifCategoryPrivate));
}

/**
 * zif_category_init:
 **/
static void
zif_category_init (ZifCategory *category)
{
	category->priv = ZIF_CATEGORY_GET_PRIVATE (category);
}

/**
 * zif_category_finalize:
 **/
static void
zif_category_finalize (GObject *object)
{
	ZifCategory *category = ZIF_CATEGORY (object);
	ZifCategoryPrivate *priv = category->priv;

	g_free (priv->parent_id);
	g_free (priv->cat_id);
	g_free (priv->name);
	g_free (priv->summary);
	g_free (priv->icon);

	G_OBJECT_CLASS (zif_category_parent_class)->finalize (object);
}

/**
 * zif_category_new:
 *
 * Return value: a new ZifCategory object.
 *
 * Since: 0.5.4
 **/
ZifCategory *
zif_category_new (void)
{
	ZifCategory *category;
	category = g_object_new (ZIF_TYPE_CATEGORY, NULL);
	return ZIF_CATEGORY (category);
}

