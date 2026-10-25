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
#include <math.h>

#include "velocity-speedtest.h"
#include "velocity-window.h"
#define GAUGE_MAX_MBPS 1000.0

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
      gtk_label_set_label (self->server_label, _ ("Ready to test"));
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
  gtk_widget_class_bind_template_callback (widget_class, on_start_clicked);
}

static void
velocity_window_init (VelocityWindow *self)
{
  PangoAttrList *big = pango_attr_list_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  if (g_str_has_suffix (APP_ID, ".Devel"))
    gtk_widget_add_css_class (GTK_WIDGET (self), "devel");

  // the big goofy number in the middle of the gauge
  pango_attr_list_insert (big, pango_attr_scale_new (4.2));
  pango_attr_list_insert (big, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes (self->speed_label, big);
  pango_attr_list_unref (big);

  gtk_drawing_area_set_draw_func (self->gauge, gauge_draw, self, NULL);

  self->test = velocity_speedtest_new ();
  g_signal_connect (self->test, "phase-changed", G_CALLBACK (on_phase_changed), self);
  g_signal_connect (self->test, "progress", G_CALLBACK (on_progress), self);
  g_signal_connect (self->test, "failed", G_CALLBACK (on_failed), self);
}
