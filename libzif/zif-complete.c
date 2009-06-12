/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <libsoup/soup.h>

#include "zif-complete.h"

#include "egg-debug.h"

#define ZIF_COMPLETE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_COMPLETE, ZifCompletePrivate))

struct ZifCompletePrivate
{
	guint			 steps;
	guint			 current;
	guint			 last_percentage;
	ZifComplete		*child;
};

typedef enum {
	PERCENTAGE_CHANGED,
	LAST_SIGNAL
} PkSignals;

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (ZifComplete, zif_complete, G_TYPE_OBJECT)

/**
 * zif_complete_discrete_to_percent:
 * @discrete: The discrete level
 * @steps: The number of discrete steps
 *
 * We have to be carefull when converting from discrete->%.
 *
 * Return value: The percentage for this discrete value.
 **/
static guint
zif_complete_discrete_to_percent (guint discrete, guint steps)
{
	/* check we are in range */
	if (discrete > steps)
		return 100;
	if (steps == 0) {
		egg_warning ("steps is 0!");
		return 0;
	}
	return (guint) ((gfloat) discrete * (100.0f / (gfloat) (steps)));
}

/**
 * zif_complete_emit_progress_changed:
 **/
static void
zif_complete_emit_progress_changed (ZifComplete *complete, guint percentage)
{
	/* is it less */
	if (percentage < complete->priv->last_percentage) {
		egg_warning ("percentage cannot go down from %i to %i!", complete->priv->last_percentage, percentage);
		return;
	}

	/* is it the same */
	if (percentage == complete->priv->last_percentage) {
		egg_debug ("ignoring same percentage value as last");
		return;
	}

	/* emit and save */
	egg_warning ("emitting percentage=%i on %p", percentage, complete);
	g_signal_emit (complete, signals [PERCENTAGE_CHANGED], 0, percentage);
	complete->priv->last_percentage = percentage;
}

/**
 * zif_complete_progress_changed_cb:
 **/
static void
zif_complete_progress_changed_cb (ZifComplete *child, guint value, ZifComplete *complete)
{
	guint offset;
	guint range;
	guint extra;

	egg_debug ("child changed: %i", value);

	/* get the offset */
	offset = zif_complete_discrete_to_percent (complete->priv->current, complete->priv->steps);

	/* get the range between the parent step and the next parent step */
	range = zif_complete_discrete_to_percent (complete->priv->current+1, complete->priv->steps) - offset;
	if (range == 0) {
		egg_warning ("range=0, should be impossible");
		return;
	}

	/* get the extra contributed by the child */
	extra = ((gfloat) value / 100.0f) * (gfloat) range;

	/* emit from the parent */
	zif_complete_emit_progress_changed (complete, offset + extra);
}

/**
 * zif_complete_set_child:
 * @complete: the #ZifComplete object
 * @child: A child #ZifComplete to monitor
 *
 * Monitor a child completion and proxy back up to the parent completion
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_complete_set_child (ZifComplete *complete, ZifComplete *child)
{
	g_return_val_if_fail (ZIF_IS_COMPLETE (complete), FALSE);
	g_return_val_if_fail (ZIF_IS_COMPLETE (child), FALSE);
	g_return_val_if_fail (complete->priv->child == NULL, FALSE);

	/* watch this */
	complete->priv->child = g_object_ref (child);
	g_signal_connect (child, "percentage-changed", G_CALLBACK (zif_complete_progress_changed_cb), complete);

	return TRUE;
}

/**
 * zif_complete_set_number_steps:
 * @complete: the #ZifComplete object
 * @steps: The number of sub-tasks in this transaction
 *
 * Sets the number of sub-tasks, i.e. how many times the zif_complete_done()
 * function will be called in the loop.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_complete_set_number_steps (ZifComplete *complete, guint steps)
{
	g_return_val_if_fail (ZIF_IS_COMPLETE (complete), FALSE);
	g_return_val_if_fail (steps != 0, FALSE);

	egg_debug ("setting up with %i steps", steps);

	complete->priv->steps = steps;
	complete->priv->current = 0;

	return TRUE;
}

/**
 * zif_complete_done:
 * @complete: the #ZifComplete object
 *
 * Called when the current sub task has finished.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_complete_done (ZifComplete *complete)
{
	guint percentage;

	g_return_val_if_fail (ZIF_IS_COMPLETE (complete), FALSE);
	g_return_val_if_fail (complete->priv->steps > 0, FALSE);

	/* is already at 100%? */
	if (complete->priv->current == complete->priv->steps)
		return FALSE;

	/* another */
	complete->priv->current++;

	/* find new percentage */
	percentage = zif_complete_discrete_to_percent (complete->priv->current, complete->priv->steps);
	zif_complete_emit_progress_changed (complete, percentage);

	return TRUE;
}

/**
 * zif_complete_finalize:
 **/
static void
zif_complete_finalize (GObject *object)
{
	ZifComplete *complete;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_COMPLETE (object));
	complete = ZIF_COMPLETE (object);

	if (complete->priv->child != NULL)
		g_object_unref (complete->priv->child);

	G_OBJECT_CLASS (zif_complete_parent_class)->finalize (object);
}

/**
 * zif_complete_class_init:
 **/
static void
zif_complete_class_init (ZifCompleteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_complete_finalize;

	signals [PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifCompleteClass, percentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (ZifCompletePrivate));
}

/**
 * zif_complete_init:
 **/
static void
zif_complete_init (ZifComplete *complete)
{
	complete->priv = ZIF_COMPLETE_GET_PRIVATE (complete);
	complete->priv->child = NULL;
	complete->priv->steps = 0;
	complete->priv->current = 0;
	complete->priv->last_percentage = 0;
}

/**
 * zif_complete_new:
 * Return value: A new complete class instance.
 **/
ZifComplete *
zif_complete_new (void)
{
	ZifComplete *complete;
	complete = g_object_new (ZIF_TYPE_COMPLETE, NULL);
	return ZIF_COMPLETE (complete);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static guint _updates = 0;
static guint _last_percent = 0;

static void
zif_complete_test_progress_changed_cb (ZifComplete *complete, guint value, gpointer data)
{
	egg_warning ("moo");
	_last_percent = value;
	_updates++;
}

void
zif_complete_test (EggTest *test)
{
	ZifComplete *complete;
	ZifComplete *child;
	gboolean ret;

	if (!egg_test_start (test, "ZifComplete"))
		return;

	/************************************************************/
	egg_test_title (test, "get complete");
	complete = zif_complete_new ();
	egg_test_assert (test, complete != NULL);
	g_signal_connect (complete, "percentage-changed", G_CALLBACK (zif_complete_test_progress_changed_cb), NULL);

	/************************************************************/
	egg_test_title (test, "set steps");
	ret = zif_complete_set_number_steps (complete, 5);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "done one step");
	ret = zif_complete_done (complete);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "ensure 1 update");
	egg_test_assert (test, (_updates == 1));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 20));

	/************************************************************/
	egg_test_title (test, "done the rest");
	ret = zif_complete_done (complete);
	ret = zif_complete_done (complete);
	ret = zif_complete_done (complete);
	ret = zif_complete_done (complete);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "done one extra");
	ret = zif_complete_done (complete);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "ensure 5 updates");
	egg_test_assert (test, (_updates == 5));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 100));

	g_object_unref (complete);

	/* reset */
	_updates = 0;
	complete = zif_complete_new ();
	zif_complete_set_number_steps (complete, 2);
	g_signal_connect (complete, "percentage-changed", G_CALLBACK (zif_complete_test_progress_changed_cb), NULL);

	/* now test with a child */
	child = zif_complete_new ();
	zif_complete_set_number_steps (child, 2);
	zif_complete_set_child (complete, child);

	/* PARENT UPDATE */
	zif_complete_done (complete);

	/************************************************************/
	egg_test_title (test, "ensure 1 update");
	egg_test_assert (test, (_updates == 1));

	/* CHILD UPDATE */
	zif_complete_done (child);

	/************************************************************/
	egg_test_title (test, "ensure 2 updates");
	egg_test_assert (test, (_updates == 2));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 75));

	/* CHILD UPDATE */
	zif_complete_done (child);

	/************************************************************/
	egg_test_title (test, "ensure 3 updates");
	egg_test_assert (test, (_updates == 3));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 100));

	/* PARENT UPDATE */
	zif_complete_done (complete);

	/************************************************************/
	egg_test_title (test, "ensure 3 updates (and we ignored the duplicate)");
	egg_test_assert (test, (_updates == 3));

	/************************************************************/
	egg_test_title (test, "ensure still correct percent");
	egg_test_assert (test, (_last_percent == 100));

	egg_test_end (test);
}
#endif

