/* velocity-speedtest-ping.c
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

#include <math.h>

#include "velocity-speedtest-private.h"

#define PING_SAMPLES 10

static int
compare_double (gconstpointer a,
                gconstpointer b)
{
  double x = *(const double *) a;
  double y = *(const double *) b;

  return (x > y) - (x < y);
}

typedef struct
{
  VelocitySpeedtest *self;
  guint generation;
  gint64 t0;
} PingCtx;

static void ping_next (VelocitySpeedtest *self);

static void
ping_done_cb (GObject *source,
              GAsyncResult *res,
              gpointer user_data)
{
  PingCtx *p = user_data;
  VelocitySpeedtest *self = p->self;
  g_autoptr (GError) error = NULL;
  g_autoptr (GBytes) body = NULL;
  gboolean stale = (p->generation != self->generation);
  double rtt = (g_get_monotonic_time () - p->t0) / 1000.0;

  body = soup_session_send_and_read_finish (SOUP_SESSION (source), res, &error);
  g_free (p);

  if (stale)
    {
      g_object_unref (self);
      return;
    }

  if (body == NULL)
    {
      velocity_speedtest_fail (self, error != NULL ? error->message : "Latency test failed.");
      g_object_unref (self);
      return;
    }

  if (self->ping_sent > 1)
    {
      g_array_append_val (self->rtts, rtt);
      velocity_speedtest_emit_progress (self, VELOCITY_PHASE_PING, rtt);
    }

  ping_next (self);
  g_object_unref (self);
}

static void
ping_next (VelocitySpeedtest *self)
{
  if (self->ping_sent >= PING_SAMPLES + 1)
    {
      double jitter = 0.0;

      // jitter: mean absolute difference between consecutive samples, in
      // arrival order, so it must be computed before the array is sorted
      // for the median
      for (guint i = 1; i < self->rtts->len; i++)
        jitter += fabs (g_array_index (self->rtts, double, i) -
                        g_array_index (self->rtts, double, i - 1));
      self->jitter = self->rtts->len > 1 ? jitter / (self->rtts->len - 1) : 0.0;

      g_array_sort (self->rtts, compare_double);
      self->ping = g_array_index (self->rtts, double, self->rtts->len / 2);

      velocity_speedtest_start_transfer (self, VELOCITY_PHASE_DOWNLOAD);
      return;
    }

  {
    PingCtx *p = g_new0 (PingCtx, 1);
    g_autofree char *url = velocity_speedtest_build_url (self, self->ping_path, NULL);
    SoupMessage *msg = soup_message_new (SOUP_METHOD_GET, url);

    p->self = g_object_ref (self);
    p->generation = self->generation;
    p->t0 = g_get_monotonic_time ();
    self->ping_sent++;

    soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                      self->run_cancellable, ping_done_cb, p);
    g_object_unref (msg);
  }
}

void
velocity_speedtest_start_ping (VelocitySpeedtest *self)
{
  velocity_speedtest_set_phase (self, VELOCITY_PHASE_PING);
  g_array_set_size (self->rtts, 0);
  self->ping_sent = 0;
  ping_next (self);
}
