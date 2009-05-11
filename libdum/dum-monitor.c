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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <sys/types.h>
#include <utime.h>

#include "dum-utils.h"
#include "dum-monitor.h"

#include "egg-debug.h"
#include "egg-string.h"

#define DUM_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_MONITOR, DumMonitorPrivate))

struct DumMonitorPrivate
{
	GPtrArray		*array;
};

enum {
	DUM_MONITOR_SIGNAL_CHANGED,
	DUM_MONITOR_SIGNAL_LAST_SIGNAL
};

static guint signals [DUM_MONITOR_SIGNAL_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (DumMonitor, dum_monitor, G_TYPE_OBJECT)

/**
 * dum_monitor_file_monitor_cb:
 **/
static void
dum_monitor_file_monitor_cb (GFileMonitor *file_monitor, GFile *file, GFile *other, GFileMonitorEvent event, DumMonitor *monitor)
{
	gchar *filename;
	filename = g_file_get_path (file);
	egg_debug ("file changed: %s", filename);
	g_signal_emit (monitor, signals [DUM_MONITOR_SIGNAL_CHANGED], 0);
	g_free (filename);
}

/**
 * dum_monitor_add_watch:
 **/
gboolean
dum_monitor_add_watch (DumMonitor *monitor, const gchar *filename, GError **error)
{
	GFile *file;
	GError *error_local = NULL;
	gboolean ret = TRUE;
	GFileMonitor *file_monitor;

	g_return_val_if_fail (DUM_IS_MONITOR (monitor), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	/* watch this file */
	file = g_file_new_for_path (filename);
	file_monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, &error_local);
	if (file_monitor == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to add monitor: %s", error_local->message);
		g_error_free (error_local);
		g_object_unref (file_monitor);
		ret = FALSE;
		goto out;
	}

	/* setup callback */
	g_file_monitor_set_rate_limit (file_monitor, 100);
	g_signal_connect (file_monitor, "changed", G_CALLBACK (dum_monitor_file_monitor_cb), monitor);

	/* add to array */
	g_ptr_array_add (monitor->priv->array, file_monitor);
out:
	g_object_unref (file);
	return ret;
}

/**
 * dum_monitor_finalize:
 **/
static void
dum_monitor_finalize (GObject *object)
{
	DumMonitor *monitor;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_MONITOR (object));
	monitor = DUM_MONITOR (object);

	g_ptr_array_foreach (monitor->priv->array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (monitor->priv->array, TRUE);

	G_OBJECT_CLASS (dum_monitor_parent_class)->finalize (object);
}

/**
 * dum_monitor_class_init:
 **/
static void
dum_monitor_class_init (DumMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_monitor_finalize;
	signals [DUM_MONITOR_SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	g_type_class_add_private (klass, sizeof (DumMonitorPrivate));
}

/**
 * dum_monitor_init:
 **/
static void
dum_monitor_init (DumMonitor *monitor)
{
	monitor->priv = DUM_MONITOR_GET_PRIVATE (monitor);
	monitor->priv->array = g_ptr_array_new ();
}

/**
 * dum_monitor_new:
 * Return value: A new monitor class instance.
 **/
DumMonitor *
dum_monitor_new (void)
{
	DumMonitor *monitor;
	monitor = g_object_new (DUM_TYPE_MONITOR, NULL);
	return DUM_MONITOR (monitor);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
dum_monitor_test_file_monitor_cb (DumMonitor *monitor, EggTest *test)
{
	egg_test_loop_quit (test);
}

static gboolean
dum_monitor_test_touch (gpointer data)
{
	utime ("../test/repos/fedora.repo", NULL);
	return FALSE;
}

void
dum_monitor_test (EggTest *test)
{
	DumMonitor *monitor;
	gboolean ret;
	GError *error = NULL;

	if (!egg_test_start (test, "DumMonitor"))
		return;

	/************************************************************/
	egg_test_title (test, "get monitor");
	monitor = dum_monitor_new ();
	egg_test_assert (test, monitor != NULL);

	g_signal_connect (monitor, "changed", G_CALLBACK (dum_monitor_test_file_monitor_cb), test);

	/************************************************************/
	egg_test_title (test, "load");
	ret = dum_monitor_add_watch (monitor, "../test/repos/fedora.repo", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/* touch in 10ms */
	g_timeout_add (10, (GSourceFunc) dum_monitor_test_touch, NULL);

	/* wait for changed */
	egg_test_loop_wait (test, 2000);
	egg_test_loop_check (test);

	g_object_unref (monitor);

	egg_test_end (test);
}
#endif

