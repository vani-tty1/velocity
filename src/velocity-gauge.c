/* velocity-gauge.c
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

#include "velocity-accent.h"
#include "velocity-gauge.h"
#include <adwaita.h>
#include <math.h>

#define GAUGE_DEFAULT_MAX_MBPS 1000.0

struct _VelocityGauge
{
  GtkDrawingArea *area;
  GSettings *settings;
  AdwAnimation *animation;
  GdkRGBA accent;
  double fraction;
  double max;
};

static double
mbps_to_fraction (double mbps,
                  double max)
{
  if (mbps <= 0.0)
    return 0.0;

  return CLAMP (log10 (1.0 + mbps) / log10 (1.0 + max), 0.0, 1.0);
}

static void
update_max (VelocityGauge *self)
{
  int max = g_settings_get_int (self->settings, "gauge-max");

  self->max = max > 0 ? max : GAUGE_DEFAULT_MAX_MBPS;
  gtk_widget_queue_draw (GTK_WIDGET (self->area));
}

static void
on_max_setting_changed (GSettings *settings,
                        const char *key,
                        VelocityGauge *self)
{
  update_max (self);
}

static void
update_accent (VelocityGauge *self)
{
  g_autofree char *id = g_settings_get_string (self->settings, "accent-color");

  if (!velocity_accent_get_rgba (id, &self->accent))
    {
      AdwStyleManager *manager = adw_style_manager_get_default ();
      GdkRGBA *rgba = adw_style_manager_get_accent_color_rgba (manager);

      // the getter returns a newly allocated GdkRGBA
      self->accent = *rgba;
      gdk_rgba_free (rgba);
    }

  gtk_widget_queue_draw (GTK_WIDGET (self->area));
}

static void
on_accent_changed (AdwStyleManager *manager,
                   GParamSpec *pspec,
                   VelocityGauge *self)
{
  update_accent (self);
}

static void
on_accent_setting_changed (GSettings *settings,
                           const char *key,
                           VelocityGauge *self)
{
  update_accent (self);
}

static void
gauge_draw (GtkDrawingArea *area,
            cairo_t *cr,
            int width,
            int height,
            gpointer user_data)
{
  VelocityGauge *self = user_data;
  GtkWidget *widget = GTK_WIDGET (area);
  GdkRGBA fg;
  double cx = width / 2.0;
  double cy = height / 2.0;
  double line = 16.0;
  double radius = MIN (width, height) / 2.0 - line;
  double start = 0.75 * G_PI;
  double sweep = 1.5 * G_PI;

  gtk_widget_get_color (widget, &fg);

  cairo_set_line_width (cr, line);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.12);
  cairo_arc (cr, cx, cy, radius, start, start + sweep);
  cairo_stroke (cr);

  if (self->fraction > 0.002)
    {
      gdk_cairo_set_source_rgba (cr, &self->accent);
      cairo_arc (cr, cx, cy, radius, start,
                 start + sweep * self->fraction);
      cairo_stroke (cr);
    }
}

static void
animation_cb (double value,
              gpointer user_data)
{
  VelocityGauge *self = user_data;

  self->fraction = value;
  gtk_widget_queue_draw (GTK_WIDGET (self->area));
}

static void
set_target (VelocityGauge *self,
            double fraction)
{
  if (self->animation != NULL)
    adw_animation_skip (self->animation);
  g_clear_object (&self->animation);

  self->animation =
      adw_timed_animation_new (GTK_WIDGET (self->area),
                               self->fraction, fraction, 250,
                               adw_callback_animation_target_new (animation_cb,
                                                                  self, NULL));
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (self->animation),
                                  ADW_EASE_OUT_CUBIC);
  adw_animation_play (self->animation);
}

VelocityGauge *
velocity_gauge_new (GtkDrawingArea *area,
                    GSettings *settings)
{
  VelocityGauge *self = g_new0 (VelocityGauge, 1);

  self->area = area;
  self->settings = g_object_ref (settings);

  update_max (self);
  update_accent (self);
  gtk_drawing_area_set_draw_func (area, gauge_draw, self, NULL);

  g_signal_connect (settings, "changed::gauge-max",
                    G_CALLBACK (on_max_setting_changed), self);
  g_signal_connect (settings, "changed::accent-color",
                    G_CALLBACK (on_accent_setting_changed), self);
  g_signal_connect (adw_style_manager_get_default (), "notify::accent-color-rgba",
                    G_CALLBACK (on_accent_changed), self);

  return self;
}

void
velocity_gauge_free (VelocityGauge *self)
{
  if (self == NULL)
    return;

  g_signal_handlers_disconnect_by_data (self->settings, self);
  g_signal_handlers_disconnect_by_data (adw_style_manager_get_default (), self);
  gtk_drawing_area_set_draw_func (self->area, NULL, NULL, NULL);

  g_clear_object (&self->animation);
  g_clear_object (&self->settings);
  g_free (self);
}

void
velocity_gauge_set_speed (VelocityGauge *self,
                          double mbps)
{
  set_target (self, mbps_to_fraction (mbps, self->max));
}

void
velocity_gauge_reset (VelocityGauge *self)
{
  set_target (self, 0.0);
}
