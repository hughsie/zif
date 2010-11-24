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

#include "zif-marshal.h"
#include "zif-utils.h"
#include "zif-state.h"

#define ZIF_STATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STATE, ZifStatePrivate))

struct _ZifStatePrivate
{
	guint			 steps;
	guint			*step_data;
	gdouble			*step_profile;
	guint			 current;
	guint			 last_percentage;
	ZifState		*child;
	ZifState		*parent;
	gulong			 percentage_child_id;
	gulong			 subpercentage_child_id;
	gulong			 allow_cancel_child_id;
	gulong			 action_child_id;
	gchar			*id;
	gboolean		 allow_cancel_changed_state;
	gboolean		 allow_cancel;
	gboolean		 allow_cancel_child;
	GCancellable		*cancellable;
	GTimer			*timer;
	ZifStateErrorHandlerCb	 error_handler_cb;
	gpointer		 error_handler_user_data;
	gboolean		 enable_profile;
	gdouble			 global_share;
	ZifStateAction		 action;
	gchar			*action_hint;
	ZifStateAction		 last_action;
	gboolean		 report_progress;
};

enum {
	SIGNAL_PERCENTAGE_CHANGED,
	SIGNAL_SUBPERCENTAGE_CHANGED,
	SIGNAL_ALLOW_CANCEL_CHANGED,
	SIGNAL_ACTION_CHANGED,
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
 * zif_state_set_report_progress:
 * @state: the #ZifState object
 * @report_progress: if we care about percentage status
 *
 * This disables progress tracking for ZifState. This is generally a bad
 * thing to do, except when you know you cannot guess the number of steps
 * in the state.
 *
 * Using this function also reduced the amount of time spent getting a
 * child state using zif_state_get_child() as a refcounted version of
 * the parent is returned instead.
 *
 * Since: 0.1.3
 **/
void
zif_state_set_report_progress (ZifState *state, gboolean report_progress)
{
	g_return_if_fail (ZIF_IS_STATE (state));
	state->priv->report_progress = report_progress;
}

/**
 * zif_state_set_enable_profile:
 * @state: the #ZifState object
 * @enable_profile: if profiling should be enabled
 *
 * This enables profiling of ZifState. This may be useful in development,
 * but be warned; enabling profiling makes #ZifState very slow.
 *
 * Since: 0.1.3
 **/
void
zif_state_set_enable_profile (ZifState *state, gboolean enable_profile)
{
	g_return_if_fail (ZIF_IS_STATE (state));
	state->priv->enable_profile = enable_profile;
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

	/* if there is an existing child, set the handler on this too */
	if (state->priv->child != NULL)
		zif_state_set_error_handler (state->priv->child, error_handler_cb, user_data);
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
	gboolean ret = FALSE;

	/* no handler */
	if (state->priv->error_handler_cb == NULL) {
		g_debug ("no error handler installed");
		goto out;
	}

	/* run the handler */
	ret = state->priv->error_handler_cb (error, state->priv->error_handler_user_data);
	g_debug ("error handler reported %s", ret ? "IGNORE" : "FAILURE");
out:
	return ret;
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
		g_warning ("steps is 0!");
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
	g_signal_emit (state, signals [SIGNAL_ALLOW_CANCEL_CHANGED], 0,
		       state->priv->allow_cancel && state->priv->allow_cancel_child);
}

/**
 * zif_state_set_percentage:
 * @state: the #ZifState object
 * @percentage: A manual percentage value between 0% and 100%
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
	gboolean ret = FALSE;

	/* do we care */
	if (!state->priv->report_progress) {
		ret = TRUE;
		goto out;
	}

	/* is it the same */
	if (percentage == state->priv->last_percentage)
		goto out;

	/* is it invalid */
	if (percentage > 100) {
		zif_state_print_parent_chain (state, 0);
		g_warning ("percentage %i%% is invalid on %p!",
			   percentage, state);
		goto out;
	}

	/* is it less */
	if (percentage < state->priv->last_percentage) {
		zif_state_print_parent_chain (state, 0);
		g_warning ("percentage cannot go down from %i to %i on %p!",
			   state->priv->last_percentage, percentage, state);
		goto out;
	}

	/* we're done, so we're not preventing cancellation anymore */
	if (percentage == 100 && !state->priv->allow_cancel) {
		g_debug ("done, so allow cancel 1 for %p", state);
		zif_state_set_allow_cancel (state, TRUE);
	}

	/* automatically cancel any action */
	if (percentage == 100 && state->priv->action != ZIF_STATE_ACTION_UNKNOWN) {
		g_debug ("done, so cancelling action %s", zif_state_action_to_string (state->priv->action));
		zif_state_action_stop (state);
	}

	/* save */
	state->priv->last_percentage = percentage;

	/* are we so low we don't care */
	if (state->priv->global_share < 0.001)
		goto out;

	/* emit */
	g_signal_emit (state, signals [SIGNAL_PERCENTAGE_CHANGED], 0, percentage);

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_state_valid:
 * @state: the #ZifState object
 *
 * Returns if the %ZifState is a valid object and ready for use.
 * This is very useful in self-testing situations like:
 * {{{
 * g_return_val_if_fail (zif_state_valid (md), FALSE);
 * }}}
 *
 * Return value: %TRUE if the %ZifState is okay to use, and has not been
 * already used.
 *
 * Since: 0.1.2
 **/
gboolean
zif_state_valid (ZifState *state)
{
	if (state == NULL)
		return FALSE;
	if (state->priv->steps != 0) {
		zif_state_print_parent_chain (state, 0);
		g_warning ("steps not zero");
		return FALSE;
	}
	if (state->priv->current != 0) {
		zif_state_print_parent_chain (state, 0);
		g_warning ("current not zero");
		return FALSE;
	}
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
	/* are we so low we don't care */
	if (state->priv->global_share < 0.01)
		goto out;

	/* just emit */
	g_signal_emit (state, signals [SIGNAL_SUBPERCENTAGE_CHANGED], 0, percentage);
out:
	return TRUE;
}

/**
 * zif_state_action_start:
 * @state: the #ZifState object
 * @action: A %ZifStateAction, e.g. %ZIF_STATE_ACTION_DECOMPRESSING
 * @action_hint: A hint on what the action is doing, e.g. "/var/cache/yum/i386/15/koji/primary.sqlite"
 *
 * Sets the action which is being performed. This is emitted up the chain
 * to any parent %ZifState objects, using the action-changed signal.
 *
 * If a %ZifState reaches 100% then it is automatically stopped with a
 * call to zif_state_action_stop().
 *
 * It is allowed to call zif_state_action_start() more than once for a
 * given %ZifState instance.
 *
 * Return value: %TRUE if the signal was propagated, %FALSE for failure
 *
 * Since: 0.1.2
 **/
gboolean
zif_state_action_start (ZifState *state, ZifStateAction action, const gchar *action_hint)
{
	g_return_val_if_fail (ZIF_IS_STATE (state), FALSE);

	/* ignore this */
	if (action == ZIF_STATE_ACTION_UNKNOWN) {
		g_warning ("cannot set action ZIF_STATE_ACTION_UNKNOWN");
		return FALSE;
	}

	/* is different? */
	if (state->priv->action == action &&
	    g_strcmp0 (action_hint, state->priv->action_hint) == 0) {
		g_debug ("same action as before, ignoring");
		return FALSE;
	}

	/* remember for stop */
	state->priv->last_action = state->priv->action;

	/* save hint */
	g_free (state->priv->action_hint);
	state->priv->action_hint = g_strdup (action_hint);

	/* save */
	state->priv->action = action;

	/* just emit */
	g_signal_emit (state, signals [SIGNAL_ACTION_CHANGED], 0, action, action_hint);
	return TRUE;
}

/**
 * zif_state_action_stop:
 * @state: the #ZifState object
 *
 * Returns the ZifState to it's previous value.
 * It is not expected you will ever need to use this funtion.
 *
 * Return value: %TRUE if the signal was propagated, %FALSE for failure
 *
 * Since: 0.1.2
 **/
gboolean
zif_state_action_stop (ZifState *state)
{
	g_return_val_if_fail (ZIF_IS_STATE (state), FALSE);

	/* nothing ever set */
	if (state->priv->action == ZIF_STATE_ACTION_UNKNOWN) {
		g_debug ("cannot unset action ZIF_STATE_ACTION_UNKNOWN");
		return FALSE;
	}

	/* pop and reset */
	state->priv->action = state->priv->last_action;
	state->priv->last_action = ZIF_STATE_ACTION_UNKNOWN;
	if (state->priv->action_hint != NULL) {
		g_free (state->priv->action_hint);
		state->priv->action_hint = NULL;
	}

	/* just emit */
	g_signal_emit (state, signals [SIGNAL_ACTION_CHANGED], 0, state->priv->action, NULL);
	return TRUE;
}

/**
 * zif_state_get_action_hint:
 * @state: the #ZifState object
 *
 * Gets the action hint, which may be useful to the users.
 *
 * Return value: The action hint, e.g. "/var/cache/yum/i386/15/koji/primary.sqlite"
 *
 * Since: 0.1.2
 **/
const gchar *
zif_state_get_action_hint (ZifState *state)
{
	g_return_val_if_fail (ZIF_IS_STATE (state), NULL);
	return state->priv->action_hint;
}

/**
 * zif_state_get_action:
 * @state: the #ZifState object
 *
 * Gets the last set action value.
 *
 * Return value: A %ZifStateAction, e.g. %ZIF_STATE_ACTION_DECOMPRESSING
 *
 * Since: 0.1.2
 **/
ZifStateAction
zif_state_get_action (ZifState *state)
{
	g_return_val_if_fail (ZIF_IS_STATE (state), ZIF_STATE_ACTION_UNKNOWN);
	return state->priv->action;
}

/**
 * zif_state_action_to_string:
 * @action: a %ZifStateAction value
 *
 * Converts the %ZifStateAction to a string.
 *
 * Return value: a const string, or %NULL for unknown.
 *
 * Since: 0.1.2
 **/
const gchar *
zif_state_action_to_string (ZifStateAction action)
{
	if (action == ZIF_STATE_ACTION_CHECKING)
		return "checking";
	if (action == ZIF_STATE_ACTION_DOWNLOADING)
		return "downloading";
	if(action == ZIF_STATE_ACTION_LOADING_REPOS)
		return "loading-repos";
	if(action == ZIF_STATE_ACTION_DECOMPRESSING)
		return "decompressing";
	if (action == ZIF_STATE_ACTION_DEPSOLVING_INSTALL)
		return "depsolving-install";
	if (action == ZIF_STATE_ACTION_DEPSOLVING_REMOVE)
		return "depsolving-remove";
	if (action == ZIF_STATE_ACTION_DEPSOLVING_UPDATE)
		return "depsolving-update";
	if (action == ZIF_STATE_ACTION_DEPSOLVING_CONFLICTS)
		return "depsolving-conflicts";
	if (action == ZIF_STATE_ACTION_INSTALLING)
		return "installing";
	if (action == ZIF_STATE_ACTION_REMOVING)
		return "removing";
	if (action == ZIF_STATE_ACTION_PREPARING)
		return "preparing";
	if (action == ZIF_STATE_ACTION_UPDATING)
		return "updating";
	if (action == ZIF_STATE_ACTION_CLEANING)
		return "cleaning";
	if (action == ZIF_STATE_ACTION_UNKNOWN)
		return "unknown";
	return NULL;
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
	guint parent_percentage;

	/* propagate up the stack if ZifState has only one step */
	if (state->priv->steps == 1) {
		zif_state_set_percentage (state, percentage);
		return;
	}

	/* did we call done on a state that did not have a size set? */
	if (state->priv->steps == 0) {
		g_warning ("done on a state %p that did not have a size set!", state);
		return;
	}

	/* always provide two levels of signals */
	zif_state_set_subpercentage (state, percentage);

	/* already at >= 100% */
	if (state->priv->current >= state->priv->steps) {
		g_warning ("already at %i/%i steps on %p", state->priv->current, state->priv->steps, state);
		return;
	}

	/* we have to deal with non-linear steps */
	if (state->priv->step_data != NULL) {
		/* we don't store zero */
		if (state->priv->current == 0) {
			parent_percentage = percentage * state->priv->step_data[state->priv->current] / 100;
		} else {
			/* bilinearly interpolate for speed */
			parent_percentage = (((100 - percentage) * state->priv->step_data[state->priv->current-1]) +
					     (percentage * state->priv->step_data[state->priv->current])) / 100;
		}
		goto out;
	}

	/* get the offset */
	offset = zif_state_discrete_to_percent (state->priv->current, state->priv->steps);

	/* get the range between the parent step and the next parent step */
	range = zif_state_discrete_to_percent (state->priv->current+1, state->priv->steps) - offset;
	if (range < 0.01) {
		g_warning ("range=%f (from %i to %i), should be impossible", range, state->priv->current+1, state->priv->steps);
		return;
	}

	/* get the extra contributed by the child */
	extra = ((gfloat) percentage / 100.0f) * range;

	/* emit from the parent */
	parent_percentage = (guint) (offset + extra);
out:
	zif_state_set_percentage (state, parent_percentage);
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
zif_state_child_allow_cancel_changed_cb (ZifState *child, gboolean allow_cancel, ZifState *state)
{
	/* save */
	state->priv->allow_cancel_child = allow_cancel;

	/* just emit if both this and child is okay */
	g_signal_emit (state, signals [SIGNAL_ALLOW_CANCEL_CHANGED], 0,
		       state->priv->allow_cancel && state->priv->allow_cancel_child);
}

/**
 * zif_state_child_action_changed_cb:
 **/
static void
zif_state_child_action_changed_cb (ZifState *child, ZifStateAction action, const gchar *action_hint, ZifState *state)
{
	/* save */
	state->priv->action = action;

	/* just emit */
	g_signal_emit (state, signals [SIGNAL_ACTION_CHANGED], 0, action, action_hint);
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
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_STATE (state), FALSE);

	/* do we care */
	if (!state->priv->report_progress) {
		ret = TRUE;
		goto out;
	}

	/* reset values */
	state->priv->steps = 0;
	state->priv->current = 0;
	state->priv->last_percentage = 0;

	/* only use the timer if profiling; it's expensive */
	if (state->priv->enable_profile)
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
	if (state->priv->action_child_id != 0) {
		g_signal_handler_disconnect (state->priv->child, state->priv->action_child_id);
		state->priv->action_child_id = 0;
	}

	/* unref child */
	if (state->priv->child != NULL) {
		g_object_unref (state->priv->child);
		state->priv->child = NULL;
	}

	/* no more step data */
	g_free (state->priv->step_data);
	g_free (state->priv->step_profile);
	state->priv->step_data = NULL;
	state->priv->step_profile = NULL;
out:
	return ret;
}

/**
 * zif_state_set_global_share:
 **/
static void
zif_state_set_global_share (ZifState *state, gdouble global_share)
{
	state->priv->global_share = global_share;
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

	/* do we care */
	if (!state->priv->report_progress) {
		child = g_object_ref (state);
		goto out;
	}

	/* already set child */
	if (state->priv->child != NULL) {
		g_signal_handler_disconnect (state->priv->child, state->priv->percentage_child_id);
		g_signal_handler_disconnect (state->priv->child, state->priv->subpercentage_child_id);
		g_signal_handler_disconnect (state->priv->child, state->priv->allow_cancel_child_id);
		g_signal_handler_disconnect (state->priv->child, state->priv->action_child_id);
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
		g_signal_connect (child, "allow-cancel-changed", G_CALLBACK (zif_state_child_allow_cancel_changed_cb), state);
	state->priv->action_child_id =
		g_signal_connect (child, "action-changed", G_CALLBACK (zif_state_child_action_changed_cb), state);

	/* reset child */
	child->priv->current = 0;
	child->priv->last_percentage = 0;

	/* set the global share on the new child */
	zif_state_set_global_share (child, state->priv->global_share);

	/* set the error handler if one exists on the child */
	if (state->priv->error_handler_cb != NULL) {
		zif_state_set_error_handler (child,
					     state->priv->error_handler_cb,
					     state->priv->error_handler_user_data);
	}

	/* set the profile state */
	zif_state_set_enable_profile (child, state->priv->enable_profile);
out:
	return child;
}

/**
 * zif_state_set_number_steps_real:
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
	gboolean ret = FALSE;

	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (steps != 0, FALSE);

	/* do we care */
	if (!state->priv->report_progress) {
		ret = TRUE;
		goto out;
	}

	/* did we call done on a state that did not have a size set? */
	if (state->priv->steps != 0) {
		g_warning ("steps already set to %i, can't set %i! [%s]",
			     state->priv->steps, steps, strloc);
		zif_state_print_parent_chain (state, 0);
		goto out;
	}

	/* set id */
	g_free (state->priv->id);
	state->priv->id = g_strdup_printf ("%s", strloc);

	/* only use the timer if profiling; it's expensive */
	if (state->priv->enable_profile)
		g_timer_start (state->priv->timer);

	/* imply reset */
	zif_state_reset (state);

	/* set steps */
	state->priv->steps = steps;

	/* global share just got smaller */
	state->priv->global_share /= steps;

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_state_set_steps_real:
 * @value: the step weighting
 *
 * This sets the step weighting, which you will want to do if one action
 * will take a bigger chunk of time than another.
 *
 * All the values must add up to 100, and the list must end with -1.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.1.3
 **/
gboolean
zif_state_set_steps_real (ZifState *state, GError **error, const gchar *strloc, gint value, ...)
{
	va_list args;
	guint i;
	gint value_temp;
	guint total;
	gboolean ret = FALSE;

	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* do we care */
	if (!state->priv->report_progress) {
		ret = TRUE;
		goto out;
	}

	/* we must set at least one thing */
	total = value;

	/* process the valist */
	va_start (args, value);
	for (i=0;; i++) {
		value_temp = va_arg (args, gint);
		if (value_temp == -1)
			break;
		total += (guint) value_temp;
	}
	va_end (args);

	/* does not sum to 100% */
	if (total != 100) {
		g_set_error (error,
			     ZIF_STATE_ERROR,
			     ZIF_STATE_ERROR_INVALID,
			     "percentage not 100: %i",
			     total);
		goto out;
	}

	/* set step number */
	ret = zif_state_set_number_steps_real (state, i+1, strloc);
	if (!ret) {
		g_set_error (error,
			     ZIF_STATE_ERROR,
			     ZIF_STATE_ERROR_INVALID,
			     "failed to set number steps: %i",
			     i+1);
		goto out;
	}

	/* save this data */
	total = value;
	state->priv->step_data = g_new0 (guint, i+2);
	state->priv->step_profile = g_new0 (gdouble, i+2);
	state->priv->step_data[0] = total;
	va_start (args, value);
	for (i=0;; i++) {
		value_temp = va_arg (args, gint);
		if (value_temp == -1)
			break;

		/* we pre-add the data to make access simpler */
		total += (guint) value_temp;
		state->priv->step_data[i+1] = total;
	}
	va_end (args);

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_state_show_profile:
 **/
static void
zif_state_show_profile (ZifState *state)
{
	gdouble div;
	gdouble total_time = 0.0f;
	GString *result;
	guint i;
	guint uncumalitive = 0;

	/* get the total time so we can work out the divisor */
	for (i=0; i<state->priv->steps; i++)
		total_time += state->priv->step_profile[i];
	div = total_time / 100.0f;

	/* what we set */
	result = g_string_new ("steps were set as [ ");
	for (i=0; i<state->priv->steps; i++) {
		g_string_append_printf (result, "%i, ",
					state->priv->step_data[i] - uncumalitive);
		uncumalitive = state->priv->step_data[i];
	}

	/* what we _should_ have set */
	g_string_append_printf (result, "-1 ] but should have been: [ ");
	for (i=0; i<state->priv->steps; i++) {
		g_string_append_printf (result, "%.0f, ",
					state->priv->step_profile[i] / div);
	}
	g_printerr ("%s-1 ] at %s\n", result->str, state->priv->id);
	g_string_free (result, TRUE);
}

/**
 * zif_state_done_real:
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
	gdouble elapsed;
	gfloat percentage;

	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* are we cancelled */
	if (g_cancellable_is_cancelled (state->priv->cancellable)) {
		g_set_error_literal (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_CANCELLED,
				     "cancelled by user action");
		ret = FALSE;
		goto out;
	}

	/* do we care */
	if (!state->priv->report_progress)
		goto out;

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
	if (state->priv->enable_profile) {
		elapsed = g_timer_elapsed (state->priv->timer, NULL);
		if (!state->priv->allow_cancel_changed_state && state->priv->current > 0) {
			if (elapsed > 0.1f) {
				g_warning ("%.1fms between zif_state_done() and no zif_state_set_allow_cancel()", elapsed * 1000);
				zif_state_print_parent_chain (state, 0);
			}
		}

		/* save the duration in the array */
		if (state->priv->step_profile != NULL)
			state->priv->step_profile[state->priv->current] = elapsed;
		g_timer_start (state->priv->timer);
	}

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
			g_print ("child is at %i/%i steps and parent done [%s]\n",
				 child_priv->current, child_priv->steps, strloc);
			zif_state_print_parent_chain (state->priv->child, 0);
			ret = TRUE;
			/* do not abort, as we want to clean this up */
		}
	}

	/* we just checked for cancel, so it's not true to say we're blocking */
	zif_state_set_allow_cancel (state, TRUE);

	/* another */
	state->priv->current++;

	/* find new percentage */
	if (state->priv->step_data == NULL) {
		percentage = zif_state_discrete_to_percent (state->priv->current,
							    state->priv->steps);
	} else {
		/* this is cumalative, for speedy access */
		percentage = state->priv->step_data[state->priv->current - 1];
	}
	zif_state_set_percentage (state, (guint) percentage);

	/* show any profiling stats */
	if (state->priv->enable_profile &&
	    state->priv->current == state->priv->steps &&
	    state->priv->step_profile != NULL) {
		zif_state_show_profile (state);
	}

	/* reset child if it exists */
	if (state->priv->child != NULL)
		zif_state_reset (state->priv->child);
out:
	return ret;
}

/**
 * zif_state_finished_real:
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
	gboolean ret = TRUE;

	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* are we cancelled */
	if (g_cancellable_is_cancelled (state->priv->cancellable)) {
		g_set_error_literal (error, ZIF_STATE_ERROR, ZIF_STATE_ERROR_CANCELLED,
				     "cancelled by user action");
		ret = FALSE;
		goto out;
	}

	/* do we care */
	if (!state->priv->report_progress)
		goto out;

	/* is already at 100%? */
	if (state->priv->current == state->priv->steps)
		goto out;

	/* all done */
	state->priv->current = state->priv->steps;

	/* set new percentage */
	zif_state_set_percentage (state, 100);
out:
	return ret;
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
	g_free (state->priv->id);
	g_free (state->priv->action_hint);
	g_free (state->priv->step_data);
	g_free (state->priv->step_profile);
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

	signals [SIGNAL_ALLOW_CANCEL_CHANGED] =
		g_signal_new ("allow-cancel-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifStateClass, allow_cancel_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	signals [SIGNAL_ACTION_CHANGED] =
		g_signal_new ("action-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifStateClass, action_changed),
			      NULL, NULL, zif_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (ZifStatePrivate));
}

/**
 * zif_state_init:
 **/
static void
zif_state_init (ZifState *state)
{
	state->priv = ZIF_STATE_GET_PRIVATE (state);
	state->priv->id = NULL;
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
	state->priv->action_child_id = 0;
	state->priv->global_share = 1.0f;
	state->priv->action = ZIF_STATE_ACTION_UNKNOWN;
	state->priv->last_action = ZIF_STATE_ACTION_UNKNOWN;
	state->priv->timer = g_timer_new ();
	state->priv->report_progress = TRUE;
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

