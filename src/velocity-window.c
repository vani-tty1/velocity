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
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <math.h>

#include "velocity-speedtest.h"
#include "velocity-window.h"
#define GAUGE_MAX_MBPS 1000.0
#define HISTORY_MAX 50

struct _VelocityWindow
{
  AdwApplicationWindow parent_instance;

  /* Template widgets */
  GtkLabel *server_label;
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
  AdwComboRow *server_row;

  JsonArray *history;
  GSettings *settings;
  gboolean populating;
  VelocitySpeedtest *test;

  double gauge_fraction;
  double gauge_target;
  AdwAnimation *gauge_animation;
};

G_DEFINE_FINAL_TYPE (VelocityWindow, velocity_window, ADW_TYPE_APPLICATION_WINDOW)

static double
mbps_to_fraction (double mbps)
{
  if (mbps <= 0.0)
    return 0.0;

  return CLAMP (log10 (1.0 + mbps) / log10 (1.0 + GAUGE_MAX_MBPS), 0.0, 1.0);
}

static void
gauge_draw (GtkDrawingArea *area,
            cairo_t *cr,
            int width,
            int height,
            gpointer user_data)
{
  VelocityWindow *self = user_data;
  GtkWidget *widget = GTK_WIDGET (area);
  GdkRGBA fg, accent;
  double cx = width / 2.0;
  double cy = height / 2.0;
  double line = 16.0;
  double radius = MIN (width, height) / 2.0 - line;
  double start = 0.75 * G_PI;
  double sweep = 1.5 * G_PI;

  gtk_widget_get_color (widget, &fg);

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  if (!gtk_style_context_lookup_color (gtk_widget_get_style_context (widget),
                                       "accent_bg_color", &accent))
    accent = (GdkRGBA) { 0.21, 0.52, 0.89, 1.0 };
  G_GNUC_END_IGNORE_DEPRECATIONS

  cairo_set_line_width (cr, line);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.12);
  cairo_arc (cr, cx, cy, radius, start, start + sweep);
  cairo_stroke (cr);

  if (self->gauge_fraction > 0.002)
    {
      gdk_cairo_set_source_rgba (cr, &accent);
      cairo_arc (cr, cx, cy, radius, start,
                 start + sweep * self->gauge_fraction);
      cairo_stroke (cr);
    }
}

static void
gauge_animation_cb (double value,
                    gpointer user_data)
{
  VelocityWindow *self = user_data;

  self->gauge_fraction = value;
  gtk_widget_queue_draw (GTK_WIDGET (self->gauge));
}

static void
gauge_set_target (VelocityWindow *self,
                  double fraction)
{
  if (self->gauge_animation != NULL)
    adw_animation_skip (self->gauge_animation);
  g_clear_object (&self->gauge_animation);

  self->gauge_target = fraction;

  self->gauge_animation =
      adw_timed_animation_new (GTK_WIDGET (self->gauge),
                               self->gauge_fraction, fraction, 250,
                               adw_callback_animation_target_new (gauge_animation_cb,
                                                                  self, NULL));
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (self->gauge_animation),
                                  ADW_EASE_OUT_CUBIC);
  adw_animation_play (self->gauge_animation);
}

static char *
format_mbps (double mbps)
{
  if (mbps >= 1000.0)
    return g_strdup_printf (_ ("%.2f Gbit/s"), mbps / 1000.0);
  if (mbps >= 100.0)
    return g_strdup_printf (_ ("%.0f Mbit/s"), mbps);

  return g_strdup_printf (_ ("%.1f Mbit/s"), mbps);
}

static char *
format_ms (double ms,
           gboolean with_unit)
{
  if (ms < 10.0)
    return g_strdup_printf (with_unit ? _ ("%.1f ms") : "%.1f", ms);

  return g_strdup_printf (with_unit ? _ ("%.0f ms") : "%.0f", ms);
}

static char *
history_path (void)
{
  return g_build_filename (g_get_user_data_dir (), "velocity", "history.json", NULL);
}

static GtkWidget *
history_row_new (JsonObject *entry)
{
  gint64 ts = json_object_get_int_member_with_default (entry, "timestamp", 0);
  const char *server = json_object_get_string_member_with_default (entry, "server", "");
  double download = json_object_get_double_member_with_default (entry, "download", 0.0);
  double upload = json_object_get_double_member_with_default (entry, "upload", 0.0);
  double ping = json_object_get_double_member_with_default (entry, "ping", 0.0);
  g_autofree char *d = format_mbps (download);
  g_autofree char *u = format_mbps (upload);
  g_autofree char *p = format_ms (ping, TRUE);
  g_autofree char *title = g_strdup_printf ("↓ %s   ↑ %s", d, u);
  g_autoptr (GDateTime) dt = g_date_time_new_from_unix_local (ts);
  g_autofree char *when = g_date_time_format (dt, "%x %H:%M");
  g_autofree char *subtitle = g_strdup_printf ("%s · %s · %s", when, p, server);
  GtkWidget *row = adw_action_row_new ();

  adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);

  return row;
}

static char *
history_serialize (VelocityWindow *self,
                   gsize *length)
{
  g_autoptr (JsonGenerator) generator = json_generator_new ();
  g_autoptr (JsonNode) root = json_node_new (JSON_NODE_ARRAY);

  json_node_set_array (root, self->history);
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);

  return json_generator_to_data (generator, length);
}

static void
history_load (VelocityWindow *self)
{
  g_autofree char *path = history_path ();
  g_autoptr (JsonParser) parser = json_parser_new ();
  JsonNode *root;

  self->history = json_array_new ();

  if (!json_parser_load_from_file (parser, path, NULL))
    return;

  root = json_parser_get_root (parser);
  if (root == NULL || !JSON_NODE_HOLDS_ARRAY (root))
    return;

  g_clear_pointer (&self->history, json_array_unref);
  self->history = json_array_ref (json_node_get_array (root));

  for (guint i = 0; i < json_array_get_length (self->history); i++)
    {
      JsonNode *node = json_array_get_element (self->history, i);

      if (JSON_NODE_HOLDS_OBJECT (node))
        gtk_list_box_prepend (self->history_list,
                              history_row_new (json_node_get_object (node)));
    }

  if (json_array_get_length (self->history) > 0)
    gtk_stack_set_visible_child_name (self->history_stack, "list");
}

static void
history_save (VelocityWindow *self)
{
  g_autofree char *path = history_path ();
  g_autofree char *dir = g_path_get_dirname (path);
  g_autofree char *data = NULL;
  g_autoptr (GError) error = NULL;
  const char *server = velocity_speedtest_get_server (self->test);
  JsonObject *entry = json_object_new ();
  GtkListBoxRow *old;
  gsize length;

  json_object_set_int_member (entry, "timestamp", g_get_real_time () / G_USEC_PER_SEC);
  json_object_set_string_member (entry, "server", server != NULL ? server : "");
  json_object_set_double_member (entry, "download", velocity_speedtest_get_download (self->test));
  json_object_set_double_member (entry, "upload", velocity_speedtest_get_upload (self->test));
  json_object_set_double_member (entry, "ping", velocity_speedtest_get_ping (self->test));
  json_array_add_object_element (self->history, entry);

  gtk_list_box_prepend (self->history_list, history_row_new (entry));
  gtk_stack_set_visible_child_name (self->history_stack, "list");

  if (json_array_get_length (self->history) > HISTORY_MAX)
    {
      json_array_remove_element (self->history, 0);

      old = gtk_list_box_get_row_at_index (self->history_list, HISTORY_MAX);
      if (old != NULL)
        gtk_list_box_remove (self->history_list, GTK_WIDGET (old));
    }

  data = history_serialize (self, &length);
  g_mkdir_with_parents (dir, 0700);
  if (!g_file_set_contents (path, data, length, &error))
    g_warning ("Could not save history: %s", error->message);
}

static void
on_export_finished (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
  VelocityWindow *self = user_data;
  g_autoptr (GFile) file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), result, NULL);
  g_autoptr (GError) error = NULL;
  g_autofree char *data = NULL;
  gsize length;

  if (file == NULL)
    return;

  data = history_serialize (self, &length);
  if (!g_file_replace_contents (file, data, length, NULL, FALSE,
                                G_FILE_CREATE_NONE, NULL, NULL, &error))
    {
      AdwDialog *dialog = adw_alert_dialog_new (_ ("Export Failed"), error->message);

      adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "ok", _ ("_OK"));
      adw_dialog_present (dialog, GTK_WIDGET (self));
    }
}

static void
on_export_clicked (VelocityWindow *self)
{
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();

  gtk_file_dialog_set_initial_name (dialog, "speedtest-results.json");
  gtk_file_dialog_save (dialog, GTK_WINDOW (self), NULL, on_export_finished, self);
}

static const char *
selected_server_base (VelocityWindow *self)
{
  guint selected = adw_combo_row_get_selected (self->server_row);

  if (selected == 0 || selected == GTK_INVALID_LIST_POSITION)
    return NULL;

  return velocity_speedtest_get_server_base_at (self->test, selected - 1);
}

static void
on_server_selected (VelocityWindow *self)
{
  const char *base;

  if (self->populating)
    return;

  base = selected_server_base (self);
  velocity_speedtest_set_preferred_server (self->test, base);
  g_settings_set_string (self->settings, "server", base != NULL ? base : "");
}

static void
on_servers_changed (VelocitySpeedtest *test,
                    VelocityWindow *self)
{
  g_autofree char *saved = g_settings_get_string (self->settings, "server");
  g_autoptr (GtkStringList) model = gtk_string_list_new (NULL);
  guint selected = 0;

  gtk_string_list_append (model, _ ("Automatic (lowest latency)"));

  for (guint i = 0; i < velocity_speedtest_get_n_servers (test); i++)
    {
      gtk_string_list_append (model, velocity_speedtest_get_server_name_at (test, i));

      if (g_strcmp0 (velocity_speedtest_get_server_base_at (test, i), saved) == 0)
        selected = i + 1;
    }

  self->populating = TRUE;
  adw_combo_row_set_model (self->server_row, G_LIST_MODEL (model));
  adw_combo_row_set_selected (self->server_row, selected);
  self->populating = FALSE;

  velocity_speedtest_set_preferred_server (test, selected_server_base (self));
}

static void
set_live_value (VelocityWindow *self,
                VelocityPhase phase,
                double value)
{
  g_autofree char *text = NULL;

  if (phase == VELOCITY_PHASE_PING)
    {
      text = format_ms (value, FALSE);
      gtk_label_set_label (self->speed_label, text);
      gtk_label_set_label (self->unit_label, _ ("ms"));
      gauge_set_target (self, 0.0);
      return;
    }

  if (value >= 1000.0)
    text = g_strdup_printf ("%.2f", value / 1000.0);
  else if (value >= 100.0)
    text = g_strdup_printf ("%.0f", value);
  else
    text = g_strdup_printf ("%.1f", value);

  gtk_label_set_label (self->speed_label, text);
  gtk_label_set_label (self->unit_label,
                       value >= 1000.0 ? _ ("Gbit/s") : _ ("Mbit/s"));
  gauge_set_target (self, mbps_to_fraction (value));
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
      gtk_label_set_label (self->speed_label, "0");
      gtk_label_set_label (self->unit_label, _ ("Mbit/s"));
      gauge_set_target (self, 0.0);
      {
        g_autofree char *p = format_ms (velocity_speedtest_get_ping (test), TRUE);
        g_autofree char *j = g_strdup_printf (_ ("%.1f ms"),
                                              velocity_speedtest_get_jitter (test));

        gtk_label_set_label (self->ping_value, p);
        gtk_label_set_label (self->jitter_value, j);
      }
      break;
    case VELOCITY_PHASE_UPLOAD:
      status = _ ("Testing upload speed…");
      gtk_label_set_label (self->speed_label, "0");
      gauge_set_target (self, 0.0);
      {
        g_autofree char *d = format_mbps (velocity_speedtest_get_download (test));

        gtk_label_set_label (self->download_value, d);
      }
      break;
    case VELOCITY_PHASE_DONE:
      {
        g_autofree char *u = format_mbps (velocity_speedtest_get_upload (test));

        gtk_label_set_label (self->upload_value, u);
      }
      history_save (self);
      status = _ ("Test complete");
      button = _ ("Test Again");
      break;
    case VELOCITY_PHASE_FAILED:
      button = _ ("Try Again");
      break;
    case VELOCITY_PHASE_IDLE:
      status = _ ("Test cancelled");
      button = _ ("Start");
      gtk_label_set_label (self->speed_label, "0");
      gtk_label_set_label (self->unit_label, _ ("Mbit/s"));
      gauge_set_target (self, 0.0);
      break;
    default:
      g_assert_not_reached ();
    }

  if (status != NULL)
    gtk_label_set_label (self->phase_label, status);

  gtk_button_set_label (self->start_button, button);

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
on_failed (VelocitySpeedtest *test,
           const char *message,
           VelocityWindow *self)
{
  gtk_label_set_label (self->phase_label, message);
  gtk_label_set_label (self->server_label, _ ("Test failed"));
  gauge_set_target (self, 0.0);
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
      gtk_label_set_label (self->server_label, _ ("Ready"));
      velocity_speedtest_start (self->test);
      break;
    default:
      g_assert_not_reached ();
    }
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
  g_clear_object (&self->gauge_animation);
  g_clear_pointer (&self->history, json_array_unref);
  g_clear_object (&self->settings);

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
  gtk_widget_class_bind_template_callback (widget_class, on_server_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_export_clicked);
}

static void
velocity_window_init (VelocityWindow *self)
{
  PangoAttrList *big = pango_attr_list_new ();

  self->populating = TRUE;
  gtk_widget_init_template (GTK_WIDGET (self));
  self->populating = FALSE;

  self->settings = g_settings_new ("io.github.vani_tty1.velocity");

  if (g_str_has_suffix (APP_ID, ".Devel"))
    gtk_widget_add_css_class (GTK_WIDGET (self), "devel");

  // the big goofy number in the middle of the gauge
  pango_attr_list_insert (big, pango_attr_scale_new (4.2));
  pango_attr_list_insert (big, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes (self->speed_label, big);
  pango_attr_list_unref (big);

  gtk_drawing_area_set_draw_func (self->gauge, gauge_draw, self, NULL);

  history_load (self);

  self->test = velocity_speedtest_new ();
  g_signal_connect (self->test, "phase-changed", G_CALLBACK (on_phase_changed), self);
  g_signal_connect (self->test, "progress", G_CALLBACK (on_progress), self);
  g_signal_connect (self->test, "failed", G_CALLBACK (on_failed), self);
  g_signal_connect (self->test, "servers-changed", G_CALLBACK (on_servers_changed), self);
  velocity_speedtest_load_servers (self->test);
}

