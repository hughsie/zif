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
	gboolean		 allow_cancel;
	gboolean		 on_console;
	gboolean		 started;
	gchar			*action;
	gchar			*detail;
	guint64			 speed;
	guint			 last_strlen_detail;
	guint			 padding;
	guint			 percentage;
	guint			 size;
};

#define ZIF_PROGRESS_BAR_PERCENTAGE_INVALID	101

G_DEFINE_TYPE (ZifProgressBar, zif_progress_bar, G_TYPE_OBJECT)

/**
 * zif_progress_bar_redraw:
 **/
static void
zif_progress_bar_redraw (ZifProgressBar *progress_bar)
{
	guint section;
	guint i;
	gchar *speed_tmp = NULL;
	ZifProgressBarPrivate *priv = progress_bar->priv;

	/* save cursor */
	g_print ("%c7", 0x1B);

	/* print action */
	if (priv->action != NULL) {
		section = strlen (priv->action);
		g_print ("%s", priv->action);
	} else {
		section = 0;
	}
	for (i=section; i<priv->padding+1; i++)
		g_print (" ");

	section = (guint) ((gfloat) priv->size / (gfloat) 100.0 * (gfloat) priv->percentage);
	g_print ("[");
	for (i=0; i<section; i++)
		g_print ("=");
	for (i=0; i<priv->size - section; i++)
		g_print (" ");
	g_print ("] ");
	if (priv->percentage != ZIF_PROGRESS_BAR_PERCENTAGE_INVALID)
		g_print ("%c%i%%%c ",
			 priv->allow_cancel ? '(' : '<',
			 priv->percentage,
			 priv->allow_cancel ? ')' : '>');
	else
		g_print ("       ");

	/* print detail */
	if (priv->detail != NULL) {
		section = strlen (priv->detail);
		g_print (" %s", priv->detail);
	} else {
		section = 0;
	}

	/* print speed */
	if (priv->speed != 0) {
		speed_tmp = g_format_size_for_display (priv->speed);
		g_print (" [%s/s]", speed_tmp);
		section += strlen (speed_tmp) + 6;
		g_free (speed_tmp);
	}

	for (i=section; i<priv->last_strlen_detail; i++)
		g_print (" ");
	priv->last_strlen_detail = section + 1;

	/* restore cursor */
	g_print ("%c8", 0x1B);

	/* started */
	priv->started = TRUE;
}

/**
 * zif_progress_bar_set_on_console:
 *
 **/
void
zif_progress_bar_set_on_console (ZifProgressBar *progress_bar, gboolean on_console)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));
	progress_bar->priv->on_console = on_console;
}

/**
 * zif_progress_bar_set_padding:
 *
 *                          /----(percentage)
 *  Action         [========        ] <51%>  Detail text
 *  \--(padding)--/\-----(size)----/  \---/
 *                                      (allow cancel)
 **/
void
zif_progress_bar_set_padding (ZifProgressBar *progress_bar, guint padding)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));
	progress_bar->priv->padding = padding;
}

/**
 * zif_progress_bar_set_size:
 **/
void
zif_progress_bar_set_size (ZifProgressBar *progress_bar, guint size)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));
	progress_bar->priv->size = size;
}

/**
 * zif_progress_bar_set_detail:
 **/
void
zif_progress_bar_set_detail (ZifProgressBar *progress_bar, const gchar *detail)
{
	/* check for old value */
	if (g_strcmp0 (detail, progress_bar->priv->detail) == 0)
		return;
	g_free (progress_bar->priv->detail);
	progress_bar->priv->detail = g_strdup (detail);

	/* no console */
	if (!progress_bar->priv->on_console) {
		if (detail != NULL)
			g_print ("Detail: %s\n", detail);
		return;
	}

	/* redraw */
	zif_progress_bar_redraw (progress_bar);
}

/**
 * zif_progress_bar_set_action:
 **/
void
zif_progress_bar_set_action (ZifProgressBar *progress_bar, const gchar *action)
{
	/* check for old value */
	if (g_strcmp0 (action, progress_bar->priv->action) == 0)
		return;
	g_free (progress_bar->priv->action);
	progress_bar->priv->action = g_strdup (action);

	/* no console */
	if (!progress_bar->priv->on_console) {
		if (action != NULL)
			g_print ("Action: %s\n", action);
		return;
	}

	/* redraw */
	zif_progress_bar_redraw (progress_bar);
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
	if (percentage == progress_bar->priv->percentage)
		return;
	progress_bar->priv->percentage = percentage;

	/* no console */
	if (!progress_bar->priv->on_console) {
		g_print ("Percentage: %i\n", percentage);
		return;
	}

	/* redraw */
	zif_progress_bar_redraw (progress_bar);
}

/**
 * zif_progress_bar_set_allow_cancel:
 **/
void
zif_progress_bar_set_allow_cancel (ZifProgressBar *progress_bar, gboolean allow_cancel)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));

	/* check for old value */
	if (progress_bar->priv->allow_cancel == allow_cancel)
		return;
	progress_bar->priv->allow_cancel = allow_cancel;

	/* no console */
	if (!progress_bar->priv->on_console) {
		g_print ("Allow cancel: %s\n", allow_cancel ? "TRUE" : "FALSE");
		return;
	}

	/* redraw */
	zif_progress_bar_redraw (progress_bar);
}

/**
 * zif_progress_bar_set_speed:
 **/
void
zif_progress_bar_set_speed (ZifProgressBar *progress_bar, guint64 speed)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));

	/* check for old value */
	if (progress_bar->priv->speed == speed)
		return;
	progress_bar->priv->speed = speed;

	/* no console */
	if (!progress_bar->priv->on_console) {
		g_print ("Speed: %" G_GUINT64_FORMAT " bytes/sec\n", speed);
		return;
	}

	/* redraw */
	zif_progress_bar_redraw (progress_bar);
}

/**
 * zif_progress_bar_start:
 **/
void
zif_progress_bar_start (ZifProgressBar *progress_bar, const gchar *text)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));

	/* just proxy */
	zif_progress_bar_set_percentage (progress_bar, 0);
	zif_progress_bar_set_detail (progress_bar, 0);
	zif_progress_bar_set_action (progress_bar, text);

	/* no console */
	if (!progress_bar->priv->on_console)
		return;

	zif_progress_bar_redraw (progress_bar);
}

/**
 * zif_progress_bar_end:
 **/
void
zif_progress_bar_end (ZifProgressBar *progress_bar)
{
	g_return_if_fail (ZIF_IS_PROGRESS_BAR (progress_bar));

	if (!progress_bar->priv->started)
		return;

	progress_bar->priv->percentage = 100;
	progress_bar->priv->started = FALSE;

	/* don't clear */
	zif_progress_bar_set_action (progress_bar, "Completed");

	/* no console */
	if (!progress_bar->priv->on_console)
		return;

	g_print ("\n");
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
	g_free (progress_bar->priv->detail);

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
