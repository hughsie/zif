/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <glib.h>
#include <string.h>

#include "pk-progress-bar.h"

#include "egg-debug.h"

#define PK_PROGRESS_BAR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PROGRESS_BAR, PkProgressBarPrivate))

typedef struct {
	guint			 position;
	gboolean		 move_forward;
} PkProgressBarPulseState;

struct PkProgressBarPrivate
{
	guint			 size;
	guint			 percentage;
	guint			 value;
	guint			 padding;
	guint			 timer_id;
	PkProgressBarPulseState	 pulse_state;
};

#define PK_PROGRESS_BAR_PERCENTAGE_INVALID	101
#define PK_PROGRESS_BAR_PULSE_TIMEOUT		40 /* ms */

G_DEFINE_TYPE (PkProgressBar, pk_progress_bar, G_TYPE_OBJECT)

/**
 * pk_progress_bar_set_padding:
 **/
gboolean
pk_progress_bar_set_padding (PkProgressBar *self, guint padding)
{
	g_return_val_if_fail (PK_IS_PROGRESS_BAR (self), FALSE);
	g_return_val_if_fail (padding < 100, FALSE);
	self->priv->padding = padding;
	return TRUE;
}

/**
 * pk_progress_bar_set_size:
 **/
gboolean
pk_progress_bar_set_size (PkProgressBar *self, guint size)
{
	g_return_val_if_fail (PK_IS_PROGRESS_BAR (self), FALSE);
	g_return_val_if_fail (size < 100, FALSE);
	self->priv->size = size;
	return TRUE;
}

/**
 * pk_progress_bar_draw:
 **/
static gboolean
pk_progress_bar_draw (PkProgressBar *self, guint value)
{
	guint section;
	guint i;

	/* restore cursor */
	g_print ("%c8", 0x1B);

	section = (guint) ((gfloat) self->priv->size / (gfloat) 100.0 * (gfloat) value);
	g_print ("[");
	for (i=0; i<section; i++)
		g_print ("=");
	for (i=0; i<self->priv->size - section; i++)
		g_print (" ");
	g_print ("] ");
	if (self->priv->percentage != PK_PROGRESS_BAR_PERCENTAGE_INVALID)
		g_print ("(%i%%)  ", self->priv->percentage);
	else
		g_print ("        ");
	return TRUE;
}

/**
 * pk_progress_bar_set_percentage:
 **/
gboolean
pk_progress_bar_set_percentage (PkProgressBar *self, guint percentage)
{
	g_return_val_if_fail (PK_IS_PROGRESS_BAR (self), FALSE);
	g_return_val_if_fail (percentage <= PK_PROGRESS_BAR_PERCENTAGE_INVALID, FALSE);

	/* check for old value */
	if (percentage == self->priv->percentage) {
		egg_debug ("skipping as the same");
		goto out;
	}

	self->priv->percentage = percentage;
	pk_progress_bar_draw (self, self->priv->value);
out:
	return TRUE;
}

/**
 * pk_progress_bar_pulse_bar:
 **/
static gboolean
pk_progress_bar_pulse_bar (PkProgressBar *self)
{
	gint i;

	/* restore cursor */
	g_print ("%c8", 0x1B);

	if (self->priv->pulse_state.move_forward) {
		if (self->priv->pulse_state.position == self->priv->size - 1)
			self->priv->pulse_state.move_forward = FALSE;
		else
			self->priv->pulse_state.position++;
	} else if (!self->priv->pulse_state.move_forward) {
		if (self->priv->pulse_state.position == 1)
			self->priv->pulse_state.move_forward = TRUE;
		else
			self->priv->pulse_state.position--;
	}

	g_print ("[");
	for (i=0; i<(gint)self->priv->pulse_state.position-1; i++)
		g_print (" ");
	g_print ("==");
	for (i=0; i<(gint) (self->priv->size - self->priv->pulse_state.position - 1); i++)
		g_print (" ");
	g_print ("] ");
	if (self->priv->percentage != PK_PROGRESS_BAR_PERCENTAGE_INVALID)
		g_print ("(%i%%)  ", self->priv->percentage);
	else
		g_print ("        ");

	return TRUE;
}

/**
 * pk_progress_bar_draw_pulse_bar:
 **/
static void
pk_progress_bar_draw_pulse_bar (PkProgressBar *self)
{
	/* have we already got zero percent? */
	if (self->priv->timer_id != 0)
		return;
	if (TRUE) {
		self->priv->pulse_state.position = 1;
		self->priv->pulse_state.move_forward = TRUE;
		self->priv->timer_id = g_timeout_add (PK_PROGRESS_BAR_PULSE_TIMEOUT, (GSourceFunc) pk_progress_bar_pulse_bar, self);
	}
}

/**
 * pk_progress_bar_set_value:
 **/
gboolean
pk_progress_bar_set_value (PkProgressBar *self, guint value)
{
	g_return_val_if_fail (PK_IS_PROGRESS_BAR (self), FALSE);
	g_return_val_if_fail (value <= PK_PROGRESS_BAR_PERCENTAGE_INVALID, FALSE);

	/* check for old value */
	if (value == self->priv->value) {
		egg_debug ("skipping as the same");
		goto out;
	}

	/* save */
	self->priv->value = value;

	/* either pulse or display */
	if (value == PK_PROGRESS_BAR_PERCENTAGE_INVALID) {
		pk_progress_bar_draw (self, 0);
		pk_progress_bar_draw_pulse_bar (self);
	} else {
		if (self->priv->timer_id != 0) {
			g_source_remove (self->priv->timer_id);
			self->priv->timer_id = 0;
		}
		pk_progress_bar_draw (self, value);
	}
out:
	return TRUE;
}

/**
 * pk_strpad:
 * @data: the input string
 * @length: the desired length of the output string, with padding
 *
 * Returns the text padded to a length with spaces. If the string is
 * longer than length then a longer string is returned.
 *
 * Return value: The padded string
 **/
static gchar *
pk_strpad (const gchar *data, guint length)
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
 * pk_progress_bar_start:
 **/
gboolean
pk_progress_bar_start (PkProgressBar *self, const gchar *text)
{
	gchar *text_pad;

	g_return_val_if_fail (PK_IS_PROGRESS_BAR (self), FALSE);

	/* finish old value */
	if (self->priv->value != 0 && self->priv->value != 100) {
		pk_progress_bar_draw (self, self->priv->value);
	}

	/* new item */
	if (self->priv->value != 0)
		g_print ("\n");

	/* make these all the same length */
	text_pad = pk_strpad (text, self->priv->padding);
	g_print ("%s", text_pad);

	/* save cursor in new position */
	g_print ("%c7", 0x1B);

	/* reset */
	self->priv->percentage = 0;
	self->priv->value = 0;
	pk_progress_bar_draw (self, 0);

	g_free (text_pad);
	return TRUE;
}

/**
 * pk_progress_bar_end:
 **/
gboolean
pk_progress_bar_end (PkProgressBar *self)
{
	g_return_val_if_fail (PK_IS_PROGRESS_BAR (self), FALSE);

	self->priv->value = 100;
	self->priv->percentage = 100;
	pk_progress_bar_draw (self, 100);
	g_print ("\n");

	return TRUE;
}

/**
 * pk_progress_bar_finalize:
 **/
static void
pk_progress_bar_finalize (GObject *object)
{
	PkProgressBar *self;
	g_return_if_fail (PK_IS_PROGRESS_BAR (object));
	self = PK_PROGRESS_BAR (object);

	if (self->priv->timer_id != 0)
		g_source_remove (self->priv->timer_id);

	G_OBJECT_CLASS (pk_progress_bar_parent_class)->finalize (object);
}

/**
 * pk_progress_bar_class_init:
 **/
static void
pk_progress_bar_class_init (PkProgressBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_progress_bar_finalize;
	g_type_class_add_private (klass, sizeof (PkProgressBarPrivate));
}

/**
 * pk_progress_bar_init:
 **/
static void
pk_progress_bar_init (PkProgressBar *self)
{
	self->priv = PK_PROGRESS_BAR_GET_PRIVATE (self);

	self->priv->size = 10;
	self->priv->percentage = 0;
	self->priv->value = 0;
	self->priv->padding = 0;
	self->priv->timer_id = 0;
}

/**
 * pk_progress_bar_new:
 * Return value: A new progress_bar class instance.
 **/
PkProgressBar *
pk_progress_bar_new (void)
{
	PkProgressBar *self;
	self = g_object_new (PK_TYPE_PROGRESS_BAR, NULL);
	return PK_PROGRESS_BAR (self);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_progress_bar (EggTest *test)
{
	PkProgressBar *self;

	if (!egg_test_start (test, "PkProgressBar"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	self = pk_progress_bar_new ();
	if (self != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	g_object_unref (self);

	egg_test_end (test);
}
#endif

