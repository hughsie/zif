/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <string.h>

#include "zif-progress-bar.h"

#define ZIF_PROGRESS_BAR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_PROGRESS_BAR, ZifProgressBarPrivate))

struct ZifProgressBarPrivate
{
	guint			 size;
	guint			 percentage;
	guint			 padding;
	gboolean		 allow_cancel;
	gboolean		 on_console;
	gchar			*action;
	gboolean		 started;
};

#define ZIF_PROGRESS_BAR_PERCENTAGE_INVALID	101

G_DEFINE_TYPE (ZifProgressBar, zif_progress_bar, G_TYPE_OBJECT)

/**
 * zif_progress_bar_set_padding:
 **/
void
zif_progress_bar_set_on_console (ZifProgressBar *progress_bar, gboolean on_console)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));
	progress_bar->priv->on_console = on_console;
}

/**
 * zif_progress_bar_set_padding:
 **/
void
zif_progress_bar_set_padding (ZifProgressBar *progress_bar, guint padding)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));
	g_return_if_fail (padding < 100);
	progress_bar->priv->padding = padding;
}

/**
 * zif_progress_bar_set_size:
 **/
void
zif_progress_bar_set_size (ZifProgressBar *progress_bar, guint size)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));
	g_return_if_fail (size < 100);
	progress_bar->priv->size = size;
}

/**
 * zif_progress_bar_draw:
 **/
static gboolean
zif_progress_bar_draw (ZifProgressBar *progress_bar)
{
	guint section;
	guint i;

	/* restore cursor */
	g_print ("%c8", 0x1B);

	section = (guint) ((gfloat) progress_bar->priv->size / (gfloat) 100.0 * (gfloat) progress_bar->priv->percentage);
	g_print ("[");
	for (i=0; i<section; i++)
		g_print ("=");
	for (i=0; i<progress_bar->priv->size - section; i++)
		g_print (" ");
	g_print ("] ");
	if (progress_bar->priv->percentage != ZIF_PROGRESS_BAR_PERCENTAGE_INVALID)
		g_print ("%c%i%%%c  ",
			 progress_bar->priv->allow_cancel ? '(' : '<',
			 progress_bar->priv->percentage,
			 progress_bar->priv->allow_cancel ? ')' : '>');
	else
		g_print ("        ");
	if (progress_bar->priv->action != NULL) {
		section = strlen (progress_bar->priv->action);
		if (section > 75)
			progress_bar->priv->action[75] = '\0';
		g_print (" %s", progress_bar->priv->action);
	} else {
		section = 0;
	}
	for (i=section; i<75; i++)
		g_print (" ");
	return TRUE;
}

/**
 * zif_progress_bar_set_action:
 **/
void
zif_progress_bar_set_action (ZifProgressBar *progress_bar, const gchar *action)
{
	g_free (progress_bar->priv->action);
	progress_bar->priv->action = g_strdup (action);
}

/**
 * zif_progress_bar_set_percentage:
 **/
void
zif_progress_bar_set_percentage (ZifProgressBar *progress_bar, guint percentage)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));
	g_return_if_fail (percentage <= ZIF_PROGRESS_BAR_PERCENTAGE_INVALID);

	/* check for old value */
	if (percentage == progress_bar->priv->percentage) {
		g_debug ("skipping as the same");
		goto out;
	}

	progress_bar->priv->percentage = percentage;

	/* no console */
	if (!progress_bar->priv->on_console) {
		g_print ("Percentage: %i\n", percentage);
		goto out;
	}

	zif_progress_bar_draw (progress_bar);
out:
	return;
}

/**
 * zif_progress_bar_set_allow_cancel:
 **/
void
zif_progress_bar_set_allow_cancel (ZifProgressBar *progress_bar, gboolean allow_cancel)
{
	/* nothing to do */
	if (progress_bar->priv->allow_cancel == allow_cancel)
		goto out;

	/* no console */
	if (!progress_bar->priv->on_console) {
		g_print ("Allow cancel: %s\n", allow_cancel ? "TRUE" : "FALSE");
		goto out;
	}
	progress_bar->priv->allow_cancel = allow_cancel;
	zif_progress_bar_draw (progress_bar);
out:
	return;
}

/**
 * zif_strpad:
 **/
static gchar *
zif_strpad (const gchar *data, guint length)
{
	gint size;
	guint data_len;
	gchar *text;
	gchar *padding;

	if (data == NULL)
		return g_strnfill (length, ' ');

	/* ITS4: ignore, only used for formatting */
	data_len = strlen (data);

	/* calculate */
	size = (length - data_len);
	if (size <= 0)
		return g_strdup (data);

	padding = g_strnfill (size, ' ');
	text = g_strdup_printf ("%s%s", data, padding);
	g_free (padding);
	return text;
}

/**
 * zif_progress_bar_start:
 **/
gboolean
zif_progress_bar_start (ZifProgressBar *progress_bar, const gchar *text)
{
	gchar *text_pad = NULL;

	g_return_val_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar), FALSE);

	/* no console */
	if (!progress_bar->priv->on_console) {
		g_print ("Start: %s\n", text);
		goto out;
	}

	/* finish old value */
	if (progress_bar->priv->percentage != 0 &&
	    progress_bar->priv->percentage != 100) {
		zif_progress_bar_draw (progress_bar);
	}

	/* new item */
	if (progress_bar->priv->percentage != 0)
		g_print ("\n");

	/* make these all the same length */
	text_pad = zif_strpad (text, progress_bar->priv->padding);
	g_print ("%s", text_pad);

	/* save cursor in new position */
	g_print ("%c7", 0x1B);

	/* reset */
	progress_bar->priv->percentage = 0;
	zif_progress_bar_draw (progress_bar);

	/* started */
	progress_bar->priv->started = TRUE;
out:
	g_free (text_pad);
	return TRUE;
}

/**
 * zif_progress_bar_end:
 **/
gboolean
zif_progress_bar_end (ZifProgressBar *progress_bar)
{
	g_return_val_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar), FALSE);

	if (!progress_bar->priv->started)
		goto out;

	progress_bar->priv->percentage = 100;
	progress_bar->priv->started = FALSE;

	/* no console */
	if (!progress_bar->priv->on_console)
		goto out;

	zif_progress_bar_draw (progress_bar);
	g_print ("\n");
out:
	return TRUE;
}

/**
 * zif_progress_bar_finalize:
 **/
static void
zif_progress_bar_finalize (GObject *object)
{
	ZifProgressBar *progress_bar;
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (object));
	progress_bar = ZIF_PROGRESS_BAR (object);

	g_free (progress_bar->priv->action);

	G_OBJECT_CLASS (zif_progress_bar_parent_class)->finalize (object);
}

/**
 * zif_progress_bar_class_init:
 **/
static void
zif_progress_bar_class_init (ZifProgressBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_progress_bar_finalize;
	g_type_class_add_private (klass, sizeof (ZifProgressBarPrivate));
}

/**
 * zif_progress_bar_init:
 **/
static void
zif_progress_bar_init (ZifProgressBar *progress_bar)
{
	progress_bar->priv = ZIF_PROGRESS_BAR_GET_PRIVATE (progress_bar);
	progress_bar->priv->size = 10;
	progress_bar->priv->allow_cancel = TRUE;
}

/**
 * zif_progress_bar_new:
 * Return value: A new progress_bar class instance.
 **/
ZifProgressBar *
zif_progress_bar_new (void)
{
	ZifProgressBar *progress_bar;
	progress_bar = g_object_new (ZIF_TYPE_PROGRESS_BAR, NULL);
	return ZIF_PROGRESS_BAR (progress_bar);
}
