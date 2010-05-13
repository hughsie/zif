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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:zif-state
 * @short_description: A #ZifState object allows progress reporting
 *
 * Objects can use zif_state_set_percentage() if the absolute percentage
 * is known. Percentages should always go up, not down.
 *
 * Modules usually set the number of steps that are expected using
 * zif_state_set_number_steps() and then after each section is completed,
 * the zif_state_done() function should be called. This will automatically
 * call zif_state_set_percentage() with the correct values.
 *
 * #ZifState allows sub-modules to be "chained up" to the parent module
 * so that as the sub-module progresses, so does the parent.
 * The child can be reused for each section, and chains can be deep.
 *
 * To get a child object, you should use zif_state_get_child() and then
 * use the result in any sub-process. You should ensure that the child object
 * is not re-used without calling zif_state_done().
 *
 * There are a few nice touches in this module, so that if a module only has
 * one progress step, the child progress is used for updates.
 *
 *
 * <example>
 *   <title>Using a #ZifState.</title>
 *   <programlisting>
 * static void
 * _do_something (ZifState *state)
 * {
 *	ZifState *state_local;
 *
 *	// setup correct number of steps
 *	zif_state_set_number_steps (state, 2);
 *
 *	// we can't cancel this function
 *	zif_state_set_allow_cancel (state, FALSE);
 *
 *	// run a sub function
 *	state_local = zif_state_get_child (state);
 *	_do_something_else1 (state_local);
 *
 *	// this section done
 *	zif_state_done (state);
 *
 *	// run another sub function
 *	state_local = zif_state_get_child (state);
 *	_do_something_else2 (state_local);
 *
 *	// this section done (all complete)
 *	zif_state_done (state);
 * }
 *   </programlisting>
 * </example>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-utils.h"
#include "zif-state.h"

#include "egg-debug.h"

#define ZIF_STATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STATE, ZifStatePrivate))

struct _ZifStatePrivate
{
	guint			 steps;
	guint			 current;
	guint			 last_percentage;
	ZifState		*child;
	ZifState		*parent;
	gulong			 percentage_child_id;
	gulong			 subpercentage_child_id;
	gulong			 allow_cancel_child_id;
	gchar			*id;
	gboolean		 allow_cancel_changed_state;
	gboolean		 allow_cancel;
	gboolean		 allow_cancel_child;
	GCancellable		*cancellable;
	GTimer			*timer;
	ZifStateErrorHandlerCb	 error_handler_cb;
	gpointer		 error_handler_user_data;
};

enum {
	SIGNAL_PERCENTAGE_CHANGED,
	SIGNAL_SUBPERCENTAGE_CHANGED,
	SIGNAL_ALLOW_CANCEL,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (ZifState, zif_state, G_TYPE_OBJECT)


/**
 * zif_state_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_state_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_state_error");
	return quark;
}

/**
 * zif_state_set_error_handler:
 * @state: the #ZifState object
 * @error_handler_cb: a #ZifStateErrorHandlerCb which returns %FALSE if the error is fatal
 * @user_data: the user_data to be passed to the #ZifStateErrorHandlerCb
 *
 * Since: 0.1.0
 **/
void
zif_state_set_error_handler (ZifState *state, ZifStateErrorHandlerCb error_handler_cb, gpointer user_data)
{
	state->priv->error_handler_cb = error_handler_cb;
	state->priv->error_handler_user_data = user_data;
}

/**
 * zif_state_error_handler:
 * @state: the #ZifState object
 * @error: a #GError
 *
 * Return value: %FALSE if the error is fatal, %TRUE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_state_error_handler (ZifState *state, const GError *error)
{
	if (state->priv->error_handler_cb == NULL)
		return FALSE;
	return state->priv->error_handler_cb (error, state->priv->error_handler_user_data);
}


/**
 * zif_state_discrete_to_percent:
 * @discrete: The discrete level
 * @steps: The number of discrete steps
 *
 * We have to be carefull when converting from discrete->%.
 *
 * Return value: The percentage for this discrete value.
 **/
static gfloat
zif_state_discrete_to_percent (guint discrete, guint steps)
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
 * zif_state_print_parent_chain:
 **/
static void
zif_state_print_parent_chain (ZifState *state, guint level)
{
	if (state->priv->parent != NULL)
		zif_state_print_parent_chain (state->priv->parent, level + 1);
	g_print ("%i) %s (%i/%i)\n",
		 level, state->priv->id, state->priv->current, state->priv->steps);
}

/**
 * zif_state_get_cancellable:
 * @state: the #ZifState object
 *
 * Gets the #GCancellable for this operation
 *
 * Return value: the #GCancellable or %NULL
 *
 * Since: 0.1.0
 **/
GCancellable *
zif_state_get_cancellable (ZifState *state)
{
	g_return_val_if_fail (ZIF_IS_STATE (state), NULL);
	if (state->priv->cancellable == NULL)
		state->priv->cancellable = g_cancellable_new ();
	return state->priv->cancellable;
}

/**
 * zif_state_set_cancellable:
 * @state: the #ZifState object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 *
 * Sets the #GCancellable object to use. You normally don't have to call this
 * function as a cancellable is created for you at when you request it.
 * It's also safe to call this function more that once if you need to.
 *
 * Since: 0.1.0
 **/
void
zif_state_set_cancellable (ZifState *state, GCancellable *cancellable)
{
	g_return_if_fail (ZIF_IS_STATE (state));
	if (state->priv->cancellable != NULL)
		g_object_unref (state->priv->cancellable);
	state->priv->cancellable = g_object_ref (cancellable);
}

/**
 * zif_state_get_allow_cancel:
 * @state: the #ZifState object
 *
 * Gets if the sub-task (or one of it's sub-sub-tasks) is cancellable
 *
 * Return value: %TRUE if the translation has a chance of being cancelled
 *
 * Since: 0.1.0
 **/
gboolean
zif_state_get_allow_cancel (ZifState *state)
{
	g_return_val_if_fail (ZIF_IS_STATE (state), FALSE);
	return state->priv->allow_cancel && state->priv->allow_cancel_child;
}

/**
 * zif_state_set_allow_cancel:
 * @state: the #ZifState object
 * @allow_cancel: If this sub-task can be cancelled
 *
 * Set is this sub task can be cancelled safely.
 *
 * Since: 0.1.0
 **/
void
zif_state_set_allow_cancel (ZifState *state, gboolean allow_cancel)
{
	g_return_if_fail (ZIF_IS_STATE (state));

	state->priv->allow_cancel_changed_state = TRUE;

	/* quick optimisation that saves lots of signals */
	if (state->priv->allow_cancel == allow_cancel)
		return;
	state->priv->allow_cancel = allow_cancel;

	/* just emit if both this and child is okay */
	g_signal_emit (state, signals [SIGNAL_ALLOW_CANCEL], 0,
		       state->priv->allow_cancel && state->priv->allow_cancel_child);
}

/**
 * zif_state_set_percentage:
 * @state: the #ZifState object
 * @percentage: A manual percentage value
 *
 * Set a percentage manually.
 * NOTE: this must be above what was previously set, or it will be rejected.
 *
 * Return value: %TRUE if the signal was propagated, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_state_set_percentage (ZifState *state, guint percentage)
{
	/* is it the same */
	if (percentage == state->priv->last_percentage)
		goto out;

	/* is it less */
	if (percentage < state->priv->last_percentage) {
		egg_warning ("percentage cannot go down from %i to %i on %p!", state->priv->last_percentage, percentage, state);
		return FALSE;
	}

	/* we're done, so we're not preventing cancellation anymore */
	if (percentage == 100 && !state->priv->allow_cancel) {
		egg_debug ("done, so allow cancel 1 for %p", state);
		zif_state_set_allow_cancel (state, TRUE);
	}

	/* emit and save */
	g_signal_emit (state, signals [SIGNAL_PERCENTAGE_CHANGED], 0, percentage);
	state->priv->last_percentage = percentage;
out:
	return TRUE;
}

/**
 * zif_state_get_percentage:
 * @state: the #ZifState object
 *
 * Get the percentage state.
 *
 * Return value: A percentage value, or G_MAXUINT for error
 *
 * Since: 0.1.0
 **/
guint
zif_state_get_percentage (ZifState *state)
{
	return state->priv->last_percentage;
}

/**
 * zif_state_set_subpercentage:
 **/
static gboolean
zif_state_set_subpercentage (ZifState *state, guint percentage)
{
	/* just emit */
	g_signal_emit (state, signals [SIGNAL_SUBPERCENTAGE_CHANGED], 0, percentage);
	return TRUE;
}

/**
 * zif_state_child_percentage_changed_cb:
 **/
static void
zif_state_child_percentage_changed_cb (ZifState *child, guint percentage, ZifState *state)
{
	gfloat offset;
	gfloat range;
	gfloat extra;

	/* propagate up the stack if ZifState has only one step */
	if (state->priv->steps == 1) {
		zif_state_set_percentage (state, percentage);
		return;
	}

	/* did we call done on a state that did not have a size set? */
	if (state->priv->steps == 0) {
		egg_warning ("done on a state %p that did not have a size set!", state);
		return;
	}

	/* always provide two levels of signals */
	zif_state_set_subpercentage (state, percentage);

	/* already at >= 100% */
	if (state->priv->current >= state->priv->steps) {
		egg_warning ("already at %i/%i steps on %p", state->priv->current, state->priv->steps, state);
		return;
	}

	/* get the offset */
	offset = zif_state_discrete_to_percent (state->priv->current, state->priv->steps);

	/* get the range between the parent step and the next parent step */
	range = zif_state_discrete_to_percent (state->priv->current+1, state->priv->steps) - offset;
	if (range < 0.01) {
		egg_warning ("range=%f (from %i to %i), should be impossible", range, state->priv->current+1, state->priv->steps);
		return;
	}

	/* get the extra contributed by the child */
	extra = ((gfloat) percentage / 100.0f) * range;

	/* emit from the parent */
	zif_state_set_percentage (state, (guint) (offset + extra));
}

/**
 * zif_state_child_subpercentage_changed_cb:
 **/
static void
zif_state_child_subpercentage_changed_cb (ZifState *child, guint percentage, ZifState *state)
{
	/* discard this, unless the ZifState has only one step */
	if (state->priv->steps != 1)
		return;

	/* propagate up the stack as if the parent didn't exist */
	zif_state_set_subpercentage (state, percentage);
}

/**
 * zif_state_child_allow_cancel_changed_cb:
 **/
static void
zif_state_child_allow_cancel_cb (ZifState *child, gboolean allow_cancel, ZifState *state)
{
	/* save */
	state->priv->allow_cancel_child = allow_cancel;

	/* just emit if both this and child is okay */
	g_signal_emit (state, signals [SIGNAL_ALLOW_CANCEL], 0,
		       state->priv->allow_cancel && state->priv->allow_cancel_child);
}

/**
 * zif_state_reset:
 * @state: the #ZifState object
 *
 * Resets the #ZifState object to unset
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_state_reset (ZifState *state)
{
	g_return_val_if_fail (ZIF_IS_STATE (state), FALSE);

	/* reset values */
	state->priv->steps = 0;
	state->priv->current = 0;
	state->priv->last_percentage = 0;
	g_timer_start (state->priv->timer);

	/* disconnect client */
	if (state->priv->percentage_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child, state->priv->percentage_child_id);
		state->priv->percentage_child_id = 0;
	}
	if (state->priv->subpercentage_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child, state->priv->subpercentage_child_id);
		state->priv->subpercentage_child_id = 0;
	}
	if (state->priv->allow_cancel_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child, state->priv->allow_cancel_child_id);
		state->priv->allow_cancel_child_id = 0;
	}

	/* unref child */
	if (state->priv->child != NULL) {
		g_object_unref (state->priv->child);
		state->priv->child = NULL;
	}

	return TRUE;
}

/**
 * zif_state_get_child:
 * @state: the #ZifState object
 *
 * Monitor a child state and proxy back up to the parent state.
 * Yo udo not have to g_object_unref() this value.
 *
 * Return value: a new %ZifState or %NULL for failure
 *
 * Since: 0.1.0
 **/
ZifState *
zif_state_get_child (ZifState *state)
{
	ZifState *child = NULL;

	g_return_val_if_fail (ZIF_IS_STATE (state), NULL);

	/* already set child */
	if (state->priv->child != NULL) {
		g_signal_handler_disconnect (state->priv->child, state->priv->percentage_child_id);
		g_signal_handler_disconnect (state->priv->child, state->priv->subpercentage_child_id);
		g_signal_handler_disconnect (state->priv->child, state->priv->allow_cancel_child_id);
		g_object_unref (state->priv->child);
	}

	/* connect up signals */
	child = zif_state_new ();
	state->priv->child = g_object_ref (child);
	state->priv->child->priv->parent = g_object_ref (state);
	state->priv->percentage_child_id =
		g_signal_connect (child, "percentage-changed", G_CALLBACK (zif_state_child_percentage_changed_cb), state);
	state->priv->subpercentage_child_id =
		g_signal_connect (child, "subpercentage-changed", G_CALLBACK (zif_state_child_subpercentage_changed_cb), state);
	state->priv->allow_cancel_child_id =
		g_signal_connect (child, "allow-cancel-changed", G_CALLBACK (zif_state_child_allow_cancel_cb), state);

	/* reset child */
	child->priv->current = 0;
	child->priv->last_percentage = 0;

	return child;
}

/**
 * zif_state_set_number_steps:
 * @state: the #ZifState object
 * @steps: The number of sub-tasks in this transaction
 *
 * Sets the number of sub-tasks, i.e. how many times the zif_state_done()
 * function will be called in the loop.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_state_set_number_steps_real (ZifState *state, guint steps, const gchar *strloc)
{
	g_return_val_if_fail (ZIF_IS_STATE (state), FALSE);
	g_return_val_if_fail (steps != 0, FALSE);

	/* did we call done on a state that did not have a size set? */
	if (state->priv->steps != 0) {
		egg_warning ("steps already set (%i)! [%s]",
			     state->priv->steps, strloc);
		return FALSE;
	}

	/* set id */
	state->priv->id = g_strdup_printf ("%s", strloc);

	/* imply reset */
	g_timer_start (state->priv->timer);
	zif_state_reset (state);

	/* set steps */
	state->priv->steps = steps;

	return TRUE;
}

/**
 * zif_state_done:
 * @state: the #ZifState object
 * @error: A #GError or %NULL
 *
 * Called when the current sub-task has finished.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_state_done_real (ZifState *state, GError **error, const gchar *strloc)
{
	gboolean ret = TRUE;
	gfloat percentage;
	gdouble elapsed;

	g_return_val_if_fail (ZIF_IS_STATE (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* are we cancelled */
	if (g_cancellable_is_cancelled (state->priv->cancellable)) {
		g_set_error_literal (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_CANCELLED,
				     "cancelled by user action");
		ret = FALSE;
		goto out;
	}

	/* did we call done on a state that did not have a size set? */
	if (state->priv->steps == 0) {
		g_set_error (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_INVALID,
			     "done on a state %p that did not have a size set! [%s]",
			     state, strloc);
		zif_state_print_parent_chain (state, 0);
		ret = FALSE;
		goto out;
	}

	/* check the interval was too big in allow_cancel false mode */
	if (!state->priv->allow_cancel_changed_state && state->priv->current > 0) {
		elapsed = g_timer_elapsed (state->priv->timer, NULL);
		if (elapsed > 0.1f) {
			egg_warning ("%.1fms between zif_state_done() and no zif_state_set_allow_cancel()", elapsed * 1000);
			zif_state_print_parent_chain (state, 0);
		}
	}
	g_timer_start (state->priv->timer);

	/* is already at 100%? */
	if (state->priv->current == state->priv->steps) {
		g_set_error (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_INVALID,
			     "already at 100%% state [%s]", strloc);
		zif_state_print_parent_chain (state, 0);
		ret = FALSE;
		goto out;
	}

	/* is child not at 100%? */
	if (state->priv->child != NULL) {
		ZifStatePrivate *child_priv = state->priv->child->priv;
		if (child_priv->current != child_priv->steps) {
			g_set_error (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_INVALID,
				     "child is at %i/%i steps and parent done [%s]",
				     child_priv->current, child_priv->steps, strloc);
			zif_state_print_parent_chain (state->priv->child, 0);
			ret = FALSE;
			/* do not abort, as we want to clean this up */
		}
	}

	/* we just checked for cancel, so it's not true to say we're blocking */
	zif_state_set_allow_cancel (state, TRUE);

	/* another */
	state->priv->current++;

	/* find new percentage */
	percentage = zif_state_discrete_to_percent (state->priv->current, state->priv->steps);
	zif_state_set_percentage (state, (guint) percentage);

	/* reset child if it exists */
	if (state->priv->child != NULL)
		zif_state_reset (state->priv->child);
out:
	return ret;
}

/**
 * zif_state_finished:
 * @state: the #ZifState object
 * @error: A #GError or %NULL
 *
 * Called when the current sub-task wants to finish early and still complete.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_state_finished_real (ZifState *state, GError **error, const gchar *strloc)
{
	g_return_val_if_fail (ZIF_IS_STATE (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* are we cancelled */
	if (g_cancellable_is_cancelled (state->priv->cancellable)) {
		g_set_error_literal (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_CANCELLED,
				     "cancelled by user action");
		return FALSE;
	}

	/* did we call done on a state that did not have a size set? */
	if (state->priv->steps == 0) {
		g_set_error (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_INVALID,
			     "finished on a state %p that did not have a size set! [%s]",
			     state, strloc);
		return FALSE;
	}

	/* is already at 100%? */
	if (state->priv->current == state->priv->steps)
		goto out;

	/* all done */
	state->priv->current = state->priv->steps;

	/* set new percentage */
	zif_state_set_percentage (state, 100);
out:
	return TRUE;
}

/**
 * zif_state_finalize:
 **/
static void
zif_state_finalize (GObject *object)
{
	ZifState *state;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STATE (object));
	state = ZIF_STATE (object);

	/* unref child too */
	zif_state_reset (state);
	if (state->priv->parent != NULL)
		g_object_unref (state->priv->parent);
	if (state->priv->cancellable != NULL)
		g_object_unref (state->priv->cancellable);
	g_timer_destroy (state->priv->timer);

	G_OBJECT_CLASS (zif_state_parent_class)->finalize (object);
}

/**
 * zif_state_class_init:
 **/
static void
zif_state_class_init (ZifStateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_state_finalize;

	signals [SIGNAL_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifStateClass, percentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	signals [SIGNAL_SUBPERCENTAGE_CHANGED] =
		g_signal_new ("subpercentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifStateClass, subpercentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	signals [SIGNAL_ALLOW_CANCEL] =
		g_signal_new ("allow-cancel-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifStateClass, allow_cancel_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (ZifStatePrivate));
}

/**
 * zif_state_init:
 **/
static void
zif_state_init (ZifState *state)
{
	state->priv = ZIF_STATE_GET_PRIVATE (state);
	state->priv->child = NULL;
	state->priv->parent = NULL;
	state->priv->cancellable = NULL;
	state->priv->allow_cancel = TRUE;
	state->priv->allow_cancel_child = TRUE;
	state->priv->allow_cancel_changed_state = FALSE;
	state->priv->error_handler_cb = NULL;
	state->priv->error_handler_user_data = NULL;
	state->priv->steps = 0;
	state->priv->current = 0;
	state->priv->last_percentage = 0;
	state->priv->percentage_child_id = 0;
	state->priv->subpercentage_child_id = 0;
	state->priv->allow_cancel_child_id = 0;
	state->priv->timer = g_timer_new ();
}

/**
 * zif_state_new:
 *
 * Return value: A new #ZifState class instance.
 *
 * Since: 0.1.0
 **/
ZifState *
zif_state_new (void)
{
	ZifState *state;
	state = g_object_new (ZIF_TYPE_STATE, NULL);
	return ZIF_STATE (state);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static guint _updates = 0;
static guint _allow_cancel_updates = 0;
static guint _last_percent = 0;
static guint _last_subpercent = 0;

static void
zif_state_test_percentage_changed_cb (ZifState *state, guint value, gpointer data)
{
	_last_percent = value;
	_updates++;
}

static void
zif_state_test_subpercentage_changed_cb (ZifState *state, guint value, gpointer data)
{
	_last_subpercent = value;
}

static void
zif_state_test_allow_cancel_changed_cb (ZifState *state, gboolean allow_cancel, gpointer data)
{
	_allow_cancel_updates++;
}

void
zif_state_test (EggTest *test)
{
	ZifState *state;
	ZifState *child;
	gboolean ret;

	if (!egg_test_start (test, "ZifState"))
		return;

	/************************************************************/
	egg_test_title (test, "get state");
	state = zif_state_new ();
	egg_test_assert (test, state != NULL);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "subpercentage-changed", G_CALLBACK (zif_state_test_subpercentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (zif_state_test_allow_cancel_changed_cb), NULL);

	/************************************************************/
	egg_test_title (test, "get default allow cancel");
	ret = zif_state_get_allow_cancel (state);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "set allow cancel");
	zif_state_set_allow_cancel (state, TRUE);
	ret = zif_state_get_allow_cancel (state);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "unset allow cancel");
	zif_state_set_allow_cancel (state, FALSE);
	ret = zif_state_get_allow_cancel (state);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "ensure 2 update (%i)", _allow_cancel_updates);
	egg_test_assert (test, (_allow_cancel_updates == 1));

	/************************************************************/
	egg_test_title (test, "set steps");
	ret = zif_state_set_number_steps (state, 5);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "done one step");
	ret = zif_state_done (state, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "ensure 1 update");
	egg_test_assert (test, (_updates == 1));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 20));

	/************************************************************/
	egg_test_title (test, "done the rest");
	ret = zif_state_done (state, NULL);
	ret = zif_state_done (state, NULL);
	ret = zif_state_done (state, NULL);
	ret = zif_state_done (state, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "done one extra");
	ret = zif_state_done (state, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "ensure 5 updates");
	egg_test_assert (test, (_updates == 5));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 100));

	/************************************************************/
	egg_test_title (test, "ensure allow cancel as we're done");
	ret = zif_state_get_allow_cancel (state);
	egg_test_assert (test, ret);

	g_object_unref (state);

	/* reset */
	_updates = 0;
	_allow_cancel_updates = 0;
	state = zif_state_new ();
	zif_state_set_allow_cancel (state, TRUE);
	zif_state_set_number_steps (state, 2);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "subpercentage-changed", G_CALLBACK (zif_state_test_subpercentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (zif_state_test_allow_cancel_changed_cb), NULL);

	// state: |-----------------------|-----------------------|
	// step1: |-----------------------|
	// child:                         |-------------|---------|

	/* PARENT UPDATE */
	ret = zif_state_done (state, NULL);

	/************************************************************/
	egg_test_title (test, "ensure 1 update");
	egg_test_assert (test, (_updates == 1));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 50));

	/* now test with a child */
	child = zif_state_get_child (state);
	zif_state_set_number_steps (child, 2);

	/* set child non-cancellable */
	zif_state_set_allow_cancel (child, FALSE);

	/************************************************************/
	egg_test_title (test, "ensure child is disallow-cancel");
	ret = zif_state_get_allow_cancel (state);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "ensure parent is disallow-cancel");
	ret = zif_state_get_allow_cancel (state);
	egg_test_assert (test, !ret);

	/* CHILD UPDATE */
	ret = zif_state_done (child, NULL);

	/************************************************************/
	egg_test_title (test, "ensure 2 updates");
	egg_test_assert (test, (_updates == 2));

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 75));

	/* CHILD UPDATE */
	ret = zif_state_done (child, NULL);

	/************************************************************/
	egg_test_title (test, "ensure 3 updates");
	if (_updates == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _updates);

	/************************************************************/
	egg_test_title (test, "ensure correct percent");
	egg_test_assert (test, (_last_percent == 100));

	/************************************************************/
	egg_test_title (test, " ensure the child finishing cleared the allow cancel on the parent");
	ret = zif_state_get_allow_cancel (state);
	egg_test_assert (test, ret);

	/* PARENT UPDATE */
	ret = zif_state_done (state, NULL);

	/************************************************************/
	egg_test_title (test, "ensure 3 updates (and we ignored the duplicate)");
	if (_updates == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _updates);

	/************************************************************/
	egg_test_title (test, "ensure still correct percent");
	egg_test_assert (test, (_last_percent == 100));

	egg_debug ("unref state");
	g_object_unref (state);

	egg_debug ("unref child");
	g_object_unref (child);

	egg_debug ("reset");
	/* reset */
	_updates = 0;
	state = zif_state_new ();
	zif_state_set_number_steps (state, 1);
	g_signal_connect (state, "percentage-changed", G_CALLBACK (zif_state_test_percentage_changed_cb), NULL);
	g_signal_connect (state, "subpercentage-changed", G_CALLBACK (zif_state_test_subpercentage_changed_cb), NULL);
	g_signal_connect (state, "allow-cancel-changed", G_CALLBACK (zif_state_test_allow_cancel_changed_cb), NULL);

	/* now test with a child */
	child = zif_state_get_child (state);
	zif_state_set_number_steps (child, 2);

	/* CHILD SET VALUE */
	zif_state_set_percentage (child, 33);

	/************************************************************/
	egg_test_title (test, "ensure 1 updates for state with one step");
	egg_test_assert (test, (_updates == 1));

	/************************************************************/
	egg_test_title (test, "ensure using child value as parent");
	egg_test_assert (test, (_last_percent == 33));

	g_object_unref (state);
	g_object_unref (child);

	egg_test_end (test);
}
#endif

