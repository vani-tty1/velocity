/* velocity-window.c
 *
 * Copyright 2026 Giovanni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <glib/gi18n.h>

#include "velocity-format.h"
#include "velocity-gauge.h"
#include "velocity-history.h"
#include "velocity-server-picker.h"
#include "velocity-speedtest.h"
#include "velocity-window.h"

struct _VelocityWindow
{
  AdwApplicationWindow parent_instance;

  GtkLabel *server_label;
  AdwSpinner *server_spinner;
  GtkDrawingArea *gauge;
  GtkLabel *speed_label;
  GtkLabel *unit_label;
  GtkLabel *phase_label;
  GtkButton *start_button;
  GtkLabel *download_value;
  GtkLabel *upload_value;
  GtkLabel *ping_value;
  GtkLabel *jitter_value;
  GtkStack *history_stack;
  GtkListBox *history_list;
  AdwActionRow *server_row;

  VelocityGauge *speed_gauge;
  VelocityHistory *history;
  GSettings *settings;
  GtkStringList *server_names;
  guint server_selected;
  VelocitySpeedtest *test;
};

G_DEFINE_FINAL_TYPE (VelocityWindow, velocity_window, ADW_TYPE_APPLICATION_WINDOW)

static void
on_history_row_activated (VelocityWindow *self,
                          GtkListBoxRow *row)
{
  velocity_history_show_details (self->history, row);
}

static void
on_clear_clicked (VelocityWindow *self)
{
  velocity_history_confirm_clear (self->history);
}

static void
on_export_clicked (VelocityWindow *self)
{
  velocity_history_export (self->history);
}

static const char *
selected_server_base (VelocityWindow *self)
{
  if (self->server_selected == 0)
    return NULL;

  return velocity_speedtest_get_server_base_at (self->test, self->server_selected - 1);
}

static void
update_ready_server_label (VelocityWindow *self)
{
  const char *name;

  if (self->server_selected != 0)
    name = velocity_speedtest_get_server_name_at (self->test, self->server_selected - 1);
  else
    name = velocity_speedtest_get_best_server (self->test);

  if (name == NULL && velocity_speedtest_is_preloading (self->test))
    name = _ ("Loading servers...");

  gtk_label_set_label (self->server_label, name != NULL ? name : "");
}

// the spinner only shows while the test is idle, so it never sits next to
// the server of a running or finished test
static void
update_preload_spinner (VelocityWindow *self)
{
  gtk_widget_set_visible (GTK_WIDGET (self->server_spinner),
                          velocity_speedtest_is_preloading (self->test) &&
                              velocity_speedtest_get_phase (self->test) == VELOCITY_PHASE_IDLE);
}

static void
update_server_row (VelocityWindow *self)
{
  const char *name = gtk_string_list_get_string (self->server_names, self->server_selected);

  adw_action_row_set_subtitle (self->server_row, name != NULL ? name : "");
}

static void
on_server_picked (guint index,
                  gpointer user_data)
{
  VelocityWindow *self = VELOCITY_WINDOW (user_data);
  const char *base;

  self->server_selected = index;
  update_server_row (self);

  base = selected_server_base (self);
  velocity_speedtest_set_preferred_server (self->test, base);
  g_settings_set_string (self->settings, "server", base != NULL ? base : "");
  update_ready_server_label (self);
}

static void
on_server_row_activated (VelocityWindow *self)
{
  velocity_show_server_dialog (GTK_WINDOW (self), G_LIST_MODEL (self->server_names),
                               self->server_selected, on_server_picked, self);
}

static void
on_best_server_changed (VelocitySpeedtest *test,
                        VelocityWindow *self)
{
  g_autoptr (GVariant) state = velocity_speedtest_get_best_server_state (test);

  // remember it for the next launch
  if (state != NULL)
    g_settings_set_value (self->settings, "best-server", state);

  if (velocity_speedtest_get_phase (test) == VELOCITY_PHASE_IDLE)
    update_ready_server_label (self);
}

static void
on_preload_changed (VelocitySpeedtest *test,
                    VelocityWindow *self)
{
  if (velocity_speedtest_get_phase (test) == VELOCITY_PHASE_IDLE)
    update_ready_server_label (self);

  update_preload_spinner (self);
}

static void
on_servers_changed (VelocitySpeedtest *test,
                    VelocityWindow *self)
{
  g_autofree char *saved = g_settings_get_string (self->settings, "server");
  g_autoptr (GtkStringList) model = gtk_string_list_new (NULL);
  guint selected = 0;

  gtk_string_list_append (model, _ ("Automatic"));

  for (guint i = 0; i < velocity_speedtest_get_n_servers (test); i++)
    {
      gtk_string_list_append (model, velocity_speedtest_get_server_name_at (test, i));

      if (g_strcmp0 (velocity_speedtest_get_server_base_at (test, i), saved) == 0)
        selected = i + 1;
    }

  g_clear_object (&self->server_names);
  self->server_names = g_steal_pointer (&model);
  self->server_selected = selected;
  update_server_row (self);

  velocity_speedtest_set_preferred_server (test, selected_server_base (self));
}

static void
set_live_value (VelocityWindow *self,
                VelocityPhase phase,
                double value)
{
  g_autofree char *text = NULL;

  if (value >= 1000.0)
    text = g_strdup_printf ("%.2f", value / 1000.0);
  else if (value >= 100.0)
    text = g_strdup_printf ("%.0f", value);
  else
    text = g_strdup_printf ("%.1f", value);

  gtk_label_set_label (self->speed_label, text);
  gtk_label_set_label (self->unit_label,
                       value >= 1000.0 ? _ ("Gbit/s") : _ ("Mbit/s"));
  velocity_gauge_set_speed (self->speed_gauge, value);
}

static void
notify_finished (VelocityWindow *self)
{
  GApplication *app = G_APPLICATION (gtk_window_get_application (GTK_WINDOW (self)));
  g_autoptr (GNotification) notification = NULL;
  g_autofree char *d = NULL;
  g_autofree char *u = NULL;
  g_autofree char *p = NULL;
  g_autofree char *body = NULL;

  // no point notifying someone who is looking at the window,
  // unless we want to annoy people hehehehe
  if (app == NULL || gtk_window_is_active (GTK_WINDOW (self)))
    return;

  d = velocity_format_mbps (velocity_speedtest_get_download (self->test));
  u = velocity_format_mbps (velocity_speedtest_get_upload (self->test));
  p = velocity_format_ms (velocity_speedtest_get_ping (self->test), TRUE);
  body = g_strdup_printf (_ ("Download %s · Upload %s · Ping %s"), d, u, p);

  notification = g_notification_new (_ ("Speed Test Complete"));
  g_notification_set_body (notification, body);
  g_notification_set_default_action (notification, "app.show-window");

  // a new result replaces the old notification instead of stacking
  g_application_send_notification (app, "test-finished", notification);
}

static void
on_phase_changed (VelocitySpeedtest *test,
                  int phase,
                  VelocityWindow *self)
{
  const char *status = NULL;
  const char *button = _ ("Stop");

  switch ((VelocityPhase) phase)
    {
    case VELOCITY_PHASE_SERVER:
      status = _ ("Finding the best server…");
      break;
    case VELOCITY_PHASE_PING:
      status = _ ("Measuring latency…");
      if (velocity_speedtest_get_server (test) != NULL)
        gtk_label_set_label (self->server_label,
                             velocity_speedtest_get_server (test));
      break;
    case VELOCITY_PHASE_DOWNLOAD:
      status = _ ("Testing download speed…");
      if (velocity_speedtest_get_server (test) != NULL)
        gtk_label_set_label (self->server_label,
                             velocity_speedtest_get_server (test));
      gtk_label_set_label (self->speed_label, "0");
      gtk_label_set_label (self->unit_label, _ ("Mbit/s"));
      velocity_gauge_reset (self->speed_gauge);
      break;
    case VELOCITY_PHASE_UPLOAD:
      status = _ ("Testing upload speed…");
      gtk_label_set_label (self->speed_label, "0");
      velocity_gauge_reset (self->speed_gauge);
      {
        g_autofree char *d = velocity_format_mbps (velocity_speedtest_get_download (test));

        gtk_label_set_label (self->download_value, d);
      }
      break;
    case VELOCITY_PHASE_DONE:
      {
        g_autofree char *u = velocity_format_mbps (velocity_speedtest_get_upload (test));

        gtk_label_set_label (self->upload_value, u);
      }
      velocity_history_add_result (self->history, self->test);
      notify_finished (self);
      status = _ ("Test complete");
      button = _ ("Test Again");
      velocity_gauge_reset (self->speed_gauge);
      break;
    case VELOCITY_PHASE_FAILED:
      button = _ ("Try Again");
      break;
    case VELOCITY_PHASE_IDLE:
      status = _ ("Test cancelled");
      button = _ ("Start");
      gtk_label_set_label (self->speed_label, "0");
      gtk_label_set_label (self->unit_label, _ ("Mbit/s"));
      velocity_gauge_reset (self->speed_gauge);
      break;
    default:
      g_assert_not_reached ();
    }

  if (status != NULL)
    gtk_label_set_label (self->phase_label, status);

  gtk_button_set_label (self->start_button, button);
  update_preload_spinner (self);

  gtk_widget_set_sensitive (GTK_WIDGET (self->server_row),
                            (VelocityPhase) phase == VELOCITY_PHASE_DONE ||
                                (VelocityPhase) phase == VELOCITY_PHASE_FAILED ||
                                (VelocityPhase) phase == VELOCITY_PHASE_IDLE);

  if ((VelocityPhase) phase == VELOCITY_PHASE_DONE ||
      (VelocityPhase) phase == VELOCITY_PHASE_FAILED ||
      (VelocityPhase) phase == VELOCITY_PHASE_IDLE)
    {
      gtk_widget_remove_css_class (GTK_WIDGET (self->start_button), "destructive-action");
      gtk_widget_add_css_class (GTK_WIDGET (self->start_button), "suggested-action");
    }
  else
    {
      gtk_widget_remove_css_class (GTK_WIDGET (self->start_button), "suggested-action");
      gtk_widget_add_css_class (GTK_WIDGET (self->start_button), "destructive-action");
    }
}

static void
on_progress (VelocitySpeedtest *test,
             int phase,
             double value,
             VelocityWindow *self)
{
  set_live_value (self, (VelocityPhase) phase, value);
}

static void
on_ping_finished (VelocitySpeedtest *test,
                  VelocityWindow *self)
{
  g_autofree char *p = velocity_format_ms (velocity_speedtest_get_ping (test), TRUE);
  g_autofree char *j = g_strdup_printf (_ ("%.1f ms"),
                                        velocity_speedtest_get_jitter (test));

  gtk_label_set_label (self->ping_value, p);
  gtk_label_set_label (self->jitter_value, j);
}

static void
on_failed (VelocitySpeedtest *test,
           const char *message,
           VelocityWindow *self)
{
  gtk_label_set_label (self->phase_label, message);
  gtk_label_set_label (self->server_label, _ ("Test failed"));
  velocity_gauge_reset (self->speed_gauge);
}

static void
on_start_clicked (VelocityWindow *self)
{
  switch (velocity_speedtest_get_phase (self->test))
    {
    case VELOCITY_PHASE_SERVER:
    case VELOCITY_PHASE_PING:
    case VELOCITY_PHASE_DOWNLOAD:
    case VELOCITY_PHASE_UPLOAD:
      velocity_speedtest_cancel (self->test);
      break;
    case VELOCITY_PHASE_IDLE:
    case VELOCITY_PHASE_DONE:
    case VELOCITY_PHASE_FAILED:
      gtk_label_set_label (self->download_value, "—");
      gtk_label_set_label (self->upload_value, "—");
      gtk_label_set_label (self->ping_value, "—");
      gtk_label_set_label (self->jitter_value, "—");
      g_application_withdraw_notification (G_APPLICATION (gtk_window_get_application (GTK_WINDOW (self))),
                                           "test-finished");
      velocity_speedtest_start (self->test);
      break;
    default:
      g_assert_not_reached ();
    }
}

// no background probing while the window is hidden
static void
on_suspended_changed (GObject *object,
                      GParamSpec *pspec,
                      VelocityWindow *self)
{
  velocity_speedtest_set_preload_enabled (self->test,
                                          !gtk_window_is_suspended (GTK_WINDOW (self)));
}

static void
velocity_window_dispose (GObject *object)
{
  VelocityWindow *self = VELOCITY_WINDOW (object);

  if (self->test != NULL)
    {
      velocity_speedtest_cancel (self->test);
      g_signal_handlers_disconnect_by_data (self->test, self);
      g_clear_object (&self->test);
    }
  g_clear_pointer (&self->speed_gauge, velocity_gauge_free);
  g_clear_pointer (&self->history, velocity_history_free);
  g_clear_object (&self->settings);
  g_clear_object (&self->server_names);

  G_OBJECT_CLASS (velocity_window_parent_class)->dispose (object);
}

static void
velocity_window_class_init (VelocityWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = velocity_window_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/vani_tty1/velocity/velocity-window.ui");
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, server_label);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, server_spinner);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, gauge);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, speed_label);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, unit_label);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, phase_label);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, start_button);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, download_value);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, upload_value);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, ping_value);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, jitter_value);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, history_stack);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, history_list);
  gtk_widget_class_bind_template_child (widget_class, VelocityWindow, server_row);
  gtk_widget_class_bind_template_callback (widget_class, on_start_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_server_row_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_export_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_clear_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_history_row_activated);
}

static void
velocity_window_init (VelocityWindow *self)
{
  PangoAttrList *big = pango_attr_list_new ();

  self->server_names = gtk_string_list_new (NULL);
  gtk_string_list_append (self->server_names, _ ("Automatic"));

  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new ("io.github.vani_tty1.velocity");
  self->speed_gauge = velocity_gauge_new (self->gauge, self->settings);
  self->history = velocity_history_new (GTK_WINDOW (self), self->history_list,
                                        self->history_stack);

  if (g_str_has_suffix (APP_ID, ".Devel"))
    gtk_widget_add_css_class (GTK_WIDGET (self), "devel");

  // the big goofy number in the middle of the gauge
  pango_attr_list_insert (big, pango_attr_scale_new (4.2));
  pango_attr_list_insert (big, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes (self->speed_label, big);
  pango_attr_list_unref (big);

  velocity_history_load (self->history);

  self->test = velocity_speedtest_new ();
  g_signal_connect (self->test, "phase-changed", G_CALLBACK (on_phase_changed), self);
  g_signal_connect (self->test, "progress", G_CALLBACK (on_progress), self);
  g_signal_connect (self->test, "ping-finished", G_CALLBACK (on_ping_finished), self);
  g_signal_connect (self->test, "failed", G_CALLBACK (on_failed), self);
  g_signal_connect (self->test, "servers-changed", G_CALLBACK (on_servers_changed), self);
  g_signal_connect (self->test, "best-server-changed", G_CALLBACK (on_best_server_changed), self);
  g_signal_connect (self->test, "preload-changed", G_CALLBACK (on_preload_changed), self);
  g_signal_connect_object (self, "notify::suspended", G_CALLBACK (on_suspended_changed), self, 0);

  {
    g_autoptr (GVariant) cached = g_settings_get_value (self->settings, "best-server");

    velocity_speedtest_set_best_server_state (self->test, cached);
    update_ready_server_label (self);
  }

  velocity_speedtest_load_servers (self->test);
}
