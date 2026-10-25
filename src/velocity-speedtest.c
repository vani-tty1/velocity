/* velocity-speedtest.c
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

#include <libsoup/soup.h>

#include "velocity-speedtest-private.h"
#include "velocity-speedtest.h"

G_DEFINE_FINAL_TYPE (VelocitySpeedtest, velocity_speedtest, G_TYPE_OBJECT)

enum
{
  SIGNAL_PHASE_CHANGED,
  SIGNAL_PROGRESS,
  SIGNAL_FINISHED,
  SIGNAL_FAILED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

void
velocity_speedtest_set_phase (VelocitySpeedtest *self,
                              VelocityPhase phase)
{
  self->phase = phase;
  g_signal_emit (self, signals[SIGNAL_PHASE_CHANGED], 0, (int) phase);
}

void
velocity_speedtest_emit_progress (VelocitySpeedtest *self,
                                  VelocityPhase phase,
                                  double value)
{
  g_signal_emit (self, signals[SIGNAL_PROGRESS], 0, (int) phase, value);
}

void
velocity_speedtest_emit_finished (VelocitySpeedtest *self)
{
  g_signal_emit (self, signals[SIGNAL_FINISHED], 0);
}

void
velocity_speedtest_stop_tick (VelocitySpeedtest *self)
{
  if (self->tick_id != 0)
    {
      g_source_remove (self->tick_id);
      self->tick_id = 0;
    }
}

void
velocity_speedtest_cancel_everything (VelocitySpeedtest *self)
{
  self->generation++;
  velocity_speedtest_stop_tick (self);

  if (self->phase_cancellable != NULL)
    {
      g_cancellable_cancel (self->phase_cancellable);
      g_clear_object (&self->phase_cancellable);
    }
  if (self->run_cancellable != NULL)
    {
      g_cancellable_cancel (self->run_cancellable);
      g_clear_object (&self->run_cancellable);
    }
}

void
velocity_speedtest_fail (VelocitySpeedtest *self,
                         const char *message)
{
  velocity_speedtest_cancel_everything (self);
  velocity_speedtest_set_phase (self, VELOCITY_PHASE_FAILED);
  g_signal_emit (self, signals[SIGNAL_FAILED], 0, message);
}

char *
velocity_speedtest_build_url (VelocitySpeedtest *self,
                              const char *path,
                              const char *extra_query)
{
  const char *sep = strchr (path, '?') != NULL ? "&" : "?";

  return g_strdup_printf ("%s%s%sr=%u%s%s", self->base, path, sep,
                          g_random_int (),
                          extra_query != NULL ? "&" : "",
                          extra_query != NULL ? extra_query : "");
}

void
velocity_speedtest_start (VelocitySpeedtest *self)
{
  g_return_if_fail (VELOCITY_IS_SPEEDTEST (self));

  velocity_speedtest_cancel_everything (self);

  self->ping = self->jitter = self->download = self->upload = 0.0;
  g_clear_pointer (&self->server_name, g_free);
  self->run_cancellable = g_cancellable_new ();

  velocity_speedtest_select_server (self);
}

void
velocity_speedtest_cancel (VelocitySpeedtest *self)
{
  g_return_if_fail (VELOCITY_IS_SPEEDTEST (self));

  if (self->phase == VELOCITY_PHASE_IDLE ||
      self->phase == VELOCITY_PHASE_DONE ||
      self->phase == VELOCITY_PHASE_FAILED)
    return;

  velocity_speedtest_cancel_everything (self);
  velocity_speedtest_set_phase (self, VELOCITY_PHASE_IDLE);
}

VelocityPhase
velocity_speedtest_get_phase (VelocitySpeedtest *self)
{
  return self->phase;
}
const char *
velocity_speedtest_get_server (VelocitySpeedtest *self)
{
  return self->server_name;
}
double
velocity_speedtest_get_ping (VelocitySpeedtest *self)
{
  return self->ping;
}
double
velocity_speedtest_get_jitter (VelocitySpeedtest *self)
{
  return self->jitter;
}
double
velocity_speedtest_get_download (VelocitySpeedtest *self)
{
  return self->download;
}
double
velocity_speedtest_get_upload (VelocitySpeedtest *self)
{
  return self->upload;
}

static void
velocity_speedtest_finalize (GObject *object)
{
  VelocitySpeedtest *self = VELOCITY_SPEEDTEST (object);

  velocity_speedtest_cancel_everything (self);

  g_clear_object (&self->session);
  g_clear_pointer (&self->candidates, g_ptr_array_unref);
  g_clear_pointer (&self->rtts, g_array_unref);
  g_clear_pointer (&self->upload_blob, g_bytes_unref);
  g_free (self->server_name);
  g_free (self->base);
  g_free (self->dl_path);
  g_free (self->ul_path);
  g_free (self->ping_path);

  G_OBJECT_CLASS (velocity_speedtest_parent_class)->finalize (object);
}

static void
velocity_speedtest_class_init (VelocitySpeedtestClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = velocity_speedtest_finalize;

  signals[SIGNAL_PHASE_CHANGED] =
      g_signal_new ("phase-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
  signals[SIGNAL_PROGRESS] =
      g_signal_new ("progress", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_DOUBLE);
  signals[SIGNAL_FINISHED] =
      g_signal_new ("finished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIGNAL_FAILED] =
      g_signal_new ("failed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
velocity_speedtest_init (VelocitySpeedtest *self)
{
  self->session = soup_session_new_with_options ("user-agent",
                                                 "Velocity/" PACKAGE_VERSION,
                                                 "timeout", 15,
                                                 NULL);
  self->candidates = g_ptr_array_new_with_free_func ((GDestroyNotify) velocity_speedtest_candidate_free);
  self->rtts = g_array_new (FALSE, FALSE, sizeof (double));
  self->phase = VELOCITY_PHASE_IDLE;
}

VelocitySpeedtest *
velocity_speedtest_new (void)
{
  return g_object_new (VELOCITY_TYPE_SPEEDTEST, NULL);
}
