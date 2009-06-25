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

#include "zif-utils.h"
#include "zif-completion.h"

#include "egg-debug.h"

#define ZIF_COMPLETION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_COMPLETION, ZifCompletionPrivate))

struct _ZifCompletionPrivate
{
	guint			 steps;
	guint			 current;
	guint			 last_percentage;
	ZifCompletion		*child;
	gulong			 percentage_child_id;
	gulong			 subpercentage_child_id;
};

typedef enum {
	PERCENTAGE_CHANGED,
	SUBPERCENTAGE_CHANGED,
	LAST_SIGNAL
} PkSignals;

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (ZifCompletion, zif_completion, G_TYPE_OBJECT)

/**
 * zif_completion_discrete_to_percent:
 * @discrete: The discrete level
 * @steps: The number of discrete steps
 *
 * We have to be carefull when converting from discrete->%.
 *
 * Return value: The percentage for this discrete value.
 **/
static gfloat
zif_completion_discrete_to_percent (guint discrete, guint steps)
{
	/* check we are in range */
	if (discrete > steps)
		return 100;
	if (steps == 0) {
		egg_warning ("steps is 0!");
		return 0;
	}
	return ((gfloat) discrete * (100.0f / (gfloat) (steps)));
}

/**
 * zif_completion_set_percentage:
 * @completion: the #ZifCompletion object
 * @percentage: A manual percentage value
 *
 * Set a percentage manually.
 * NOTE: this must be above what was previously set, or it will be rejected.
 *
 * Return value: %TRUE if the signal was propagated, %FALSE for failure
 **/
gboolean
zif_completion_set_percentage (ZifCompletion *completion, guint percentage)
{
	/* is it the same */
	if (percentage == completion->priv->last_percentage) {
		egg_debug ("ignoring same percentage=%i on %p", percentage, completion);
		return FALSE;
	}

	/* is it less */
	if (percentage < completion->priv->last_percentage) {
		egg_warning ("percentage cannot go down from %i to %i on %p!", completion->priv->last_percentage, percentage, completion);
		zif_debug_crash ();
		return FALSE;
	}

	/* emit and save */
	egg_debug ("emitting percentage=%i on %p", percentage, completion);
	g_signal_emit (completion, signals [PERCENTAGE_CHANGED], 0, percentage);
	completion->priv->last_percentage = percentage;

	return TRUE;
}

/**
 * zif_completion_set_subpercentage:
 **/
static gboolean
zif_completion_set_subpercentage (ZifCompletion *completion, guint percentage)
{
	/* emit and save */
	egg_debug ("emitting subpercentage=%i on %p", percentage, completion);
	g_signal_emit (completion, signals [SUBPERCENTAGE_CHANGED], 0, percentage);
	return TRUE;
}

/**
 * zif_completion_child_percentage_changed_cb:
 **/
static void
zif_completion_child_percentage_changed_cb (ZifCompletion *child, guint percentage, ZifCompletion *completion)
{
	gfloat offset;
	gfloat range;
	gfloat extra;

	/* propagate up the stack if ZifCompletion has only one step */
	if (completion->priv->steps == 1) {
		egg_debug ("using child percentage as parent as only one step on %p", completion);
		zif_completion_set_percentage (completion, percentage);
		return;
	}

	/* always provide two levels of signals */
	zif_completion_set_subpercentage (completion, percentage);

	/* already at >= 100% */
	if (completion->priv->current >= completion->priv->steps) {
		egg_warning ("already at %i/%i steps on %p", completion->priv->current, completion->priv->steps, completion);
		return;
	}

	/* get the offset */
	offset = zif_completion_discrete_to_percent (completion->priv->current, completion->priv->steps);

	/* get the range between the parent step and the next parent step */
	range = zif_completion_discrete_to_percent (completion->priv->current+1, completion->priv->steps) - offset;
	if (range < 0.01) {
		egg_warning ("range=%f (from %i to %i), should be impossible", range, completion->priv->current+1, completion->priv->steps);
		return;
	}

	/* get the extra contributed by the child */
	extra = ((gfloat) percentage / 100.0f) * range;

	/* emit from the parent */
	zif_completion_set_percentage (completion, (guint) (offset + extra));
}

/**
 * zif_completion_child_subpercentage_changed_cb:
 **/
static void
zif_completion_child_subpercentage_changed_cb (ZifCompletion *child, guint percentage, ZifCompletion *completion)
{
	/* discard this, unless the ZifCompletion has only one step */
	if (completion->priv->steps != 1)
		return;

	/* propagate up the stack as if the parent didn't exist */
	egg_debug ("using child subpercentage as parent as only one step");
	zif_completion_set_subpercentage (completion, percentage);
}

/**
 * zif_completion_reset:
 * @completion: the #ZifCompletion object
 *
 * Resets the #ZifCompletion object to unset
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_completion_reset (ZifCompletion *completion)
{
	g_return_val_if_fail (ZIF_IS_COMPLETION (completion), FALSE);

	egg_debug ("resetting %p", completion);

	/* reset values */
	completion->priv->steps = 0;
	completion->priv->current = 0;
	completion->priv->last_percentage = 0;

	return TRUE;
}

/**
 * zif_completion_set_child:
 * @completion: the #ZifCompletion object
 * @child: A child #ZifCompletion to monitor
 *
 * Monitor a child completion and proxy back up to the parent completion
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_completion_set_child (ZifCompletion *completion, ZifCompletion *child)
{
	g_return_val_if_fail (ZIF_IS_COMPLETION (completion), FALSE);
	g_return_val_if_fail (ZIF_IS_COMPLETION (child), FALSE);

	/* watch this */
	if (completion->priv->child != NULL) {
		/* disconnect signals */
		g_signal_handler_disconnect (completion->priv->child, completion->priv->percentage_child_id);
		g_signal_handler_disconnect (completion->priv->child, completion->priv->subpercentage_child_id);
		g_object_unref (completion->priv->child);
	}

	/* connect up signals */
	completion->priv->child = g_object_ref (child);
	completion->priv->percentage_child_id =
		g_signal_connect (child, "percentage-changed", G_CALLBACK (zif_completion_child_percentage_changed_cb), completion);
	completion->priv->subpercentage_child_id =
		g_signal_connect (child, "subpercentage-changed", G_CALLBACK (zif_completion_child_subpercentage_changed_cb), completion);

	/* reset child */
	child->priv->current = 0;
	child->priv->last_percentage = 0;

	return TRUE;
}

/**
 * zif_completion_set_number_steps:
 * @completion: the #ZifCompletion object
 * @steps: The number of sub-tasks in this transaction
 *
 * Sets the number of sub-tasks, i.e. how many times the zif_completion_done()
 * function will be called in the loop.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_completion_set_number_steps (ZifCompletion *completion, guint steps)
{
	g_return_val_if_fail (ZIF_IS_COMPLETION (completion), FALSE);
	g_return_val_if_fail (steps != 0, FALSE);

	egg_debug ("setting up %i steps on %p", steps, completion);

	/* imply reset */
	zif_completion_reset (completion);

	/* set steps */
	completion->priv->steps = steps;

	return TRUE;
}

/**
 * zif_completion_done:
 * @completion: the #ZifCompletion object
 *
 * Called when the current sub-task has finished.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_completion_done (ZifCompletion *completion)
{
	gfloat percentage;

	g_return_val_if_fail (ZIF_IS_COMPLETION (completion), FALSE);
	g_return_val_if_fail (completion->priv->steps > 0, FALSE);

	/* is already at 100%? */
	if (completion->priv->current == completion->priv->steps) {
		egg_warning ("already at 100%% completion");
		return FALSE;
	}

	/* another */
	completion->priv->current++;

	/* find new percentage */
	percentage = zif_completion_discrete_to_percent (completion->priv->current, completion->priv->steps);
	zif_completion_set_percentage (completion, (guint) percentage);

	return TRUE;
}

/**
 * zif_completion_finalize:
 **/
static void
zif_completion_finalize (GObject *object)
{
	ZifCompletion *completion;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_COMPLETION (object));
	completion = ZIF_COMPLETION (object);

	/* free, and disconnect */
	if (completion->priv->child != NULL) {
		g_signal_handler_disconnect (completion->priv->child, completion->priv->percentage_child_id);
		g_signal_handler_disconnect (completion->priv->child, completion->priv->subpercentage_child_id);
		g_object_unref (completion->priv->child);
	}

	G_OBJECT_CLASS (zif_completion_parent_class)->finalize (object);
}

/**
 * zif_completion_class_init:
 **/
static void
zif_completion_class_init (ZifCompletionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_completion_finalize;

	signals [PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifCompletionClass, percentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	signals [SUBPERCENTAGE_CHANGED] =
		g_signal_new ("subpercentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifCompletionClass, subpercentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (ZifCompletionPrivate));
}

/**
 * zif_completion_init:
 **/
static void
zif_completion_init (ZifCompletion *completion)
{
	completion->priv = ZIF_COMPLETION_GET_PRIVATE (completion);
	completion->priv->child = NULL;
	completion->priv->steps = 0;
	completion->priv->current = 0;
	completion->priv->last_percentage = 0;
}

/**
 * zif_completion_new:
 *
 * Return value: A new #ZifCompletion class instance.
 **/
ZifCompletion *
zif_completion_new (void)
{
	ZifCompletion *completion;
	completion = g_object_new (ZIF_TYPE_COMPLETION, NULL);
	return ZIF_COMPLETION (completion);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static guint _updates = 0;
static guint _last_percent = 0;
static guint _last_subpercent = 0;

static void
zif_completion_test_percentage_changed_cb (ZifCompletion *completion, guint value, gpointer data)
{
	_last_percent = value;
	_updates++;
}

static void
zif_completion_test_subpercentage_changed_cb (ZifCompletion *completion, guint value, gpointer data)
{
	_last_subpercent = value;
}

void
zif_completion_test (EggTest *test)
{
	ZifCompletion *completion;
	ZifCompletion *child;
	gboolean ret;

	if (!egg_test_start (test, "ZifCompletion"))
		return;

	/************************************************************/
	egg_test_title (test, "get completion");
	completion = zif_completion_new ();
	egg_test_assert (test, completion != NULL);
	g_signal_connect (completion, "percentage-changed", G_CALLBACK (zif_completion_test_percentage_changed_cb), NULL);
	g_signal_connect (completion, "subpercentage-changed", G_CALLBACK (zif_completion_test_subpercentage_changed_cb), NULL);

	/************************************************************/
	egg_test_title (test, "set steps");
	ret = zif_completion_set_number_steps (completion, 5);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "done one step");
	ret = zif_completion_done (completion);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "ensure 1 update");
	egg_test_assert (test, (_updates == 1));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 20));

	/************************************************************/
	egg_test_title (test, "done the rest");
	ret = zif_completion_done (completion);
	ret = zif_completion_done (completion);
	ret = zif_completion_done (completion);
	ret = zif_completion_done (completion);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "done one extra");
	ret = zif_completion_done (completion);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "ensure 5 updates");
	egg_test_assert (test, (_updates == 5));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 100));

	g_object_unref (completion);

	/* reset */
	_updates = 0;
	completion = zif_completion_new ();
	zif_completion_set_number_steps (completion, 2);
	g_signal_connect (completion, "percentage-changed", G_CALLBACK (zif_completion_test_percentage_changed_cb), NULL);
	g_signal_connect (completion, "subpercentage-changed", G_CALLBACK (zif_completion_test_subpercentage_changed_cb), NULL);

	/* now test with a child */
	child = zif_completion_new ();
	zif_completion_set_child (completion, child);
	zif_completion_set_number_steps (child, 2);

	/* PARENT UPDATE */
	zif_completion_done (completion);

	/************************************************************/
	egg_test_title (test, "ensure 1 update");
	egg_test_assert (test, (_updates == 1));

	/* CHILD UPDATE */
	zif_completion_done (child);

	/************************************************************/
	egg_test_title (test, "ensure 2 updates");
	egg_test_assert (test, (_updates == 2));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 75));

	/* CHILD UPDATE */
	zif_completion_done (child);

	/************************************************************/
	egg_test_title (test, "ensure 3 updates");
	egg_test_assert (test, (_updates == 3));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 100));

	/* PARENT UPDATE */
	zif_completion_done (completion);

	/************************************************************/
	egg_test_title (test, "ensure 3 updates (and we ignored the duplicate)");
	egg_test_assert (test, (_updates == 3));

	/************************************************************/
	egg_test_title (test, "ensure still correct percent");
	egg_test_assert (test, (_last_percent == 100));

	g_object_unref (completion);
	g_object_unref (child);

	/* reset */
	_updates = 0;
	completion = zif_completion_new ();
	zif_completion_set_number_steps (completion, 1);
	g_signal_connect (completion, "percentage-changed", G_CALLBACK (zif_completion_test_percentage_changed_cb), NULL);
	g_signal_connect (completion, "subpercentage-changed", G_CALLBACK (zif_completion_test_subpercentage_changed_cb), NULL);

	/* now test with a child */
	child = zif_completion_new ();
	zif_completion_set_number_steps (child, 2);
	zif_completion_set_child (completion, child);

	/* CHILD SET VALUE */
	zif_completion_set_percentage (child, 33);

	/************************************************************/
	egg_test_title (test, "ensure 1 updates for completion with one step");
	egg_test_assert (test, (_updates == 1));

	/************************************************************/
	egg_test_title (test, "ensure using child value as parent");
	egg_test_assert (test, (_last_percent == 33));

	g_object_unref (completion);
	g_object_unref (child);

	egg_test_end (test);
}
#endif

