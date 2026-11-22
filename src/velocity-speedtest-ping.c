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
ping_finish (VelocitySpeedtest *self)
{
  double jitter = 0.0;

  // jitter is computed in arrival order, before the sort for the median
  for (guint i = 1; i < self->rtts->len; i++)
    jitter += fabs (g_array_index (self->rtts, double, i) -
                    g_array_index (self->rtts, double, i - 1));
  self->jitter = self->rtts->len > 1 ? jitter / (self->rtts->len - 1) : 0.0;

  if (self->rtts->len > 0)
    {
      g_array_sort (self->rtts, compare_double);
      self->ping = g_array_index (self->rtts, double, self->rtts->len / 2);
    }

  velocity_speedtest_emit_ping_finished (self);
}

static void
ping_done_cb (GObject *source,
              GAsyncResult *res,
              gpointer user_data)
{
  PingCtx *p = user_data;
  VelocitySpeedtest *self = p->self;
  g_autoptr (GError) error = NULL;
  g_autoptr (GBytes) body = NULL;
  gboolean stale = (p->generation != self->ping_generation);
  double rtt = (g_get_monotonic_time () - p->t0) / 1000.0;

  body = soup_session_send_and_read_finish (SOUP_SESSION (source), res, &error);
  g_free (p);

  if (stale)
    {
      g_object_unref (self);
      return;
    }

  // a failed ping must not abort the download, just drop the server from the cache
  if (body == NULL)
    {
      velocity_speedtest_invalidate_best_server (self);
      ping_finish (self);
      g_object_unref (self);
      return;
    }

  // the first sample is a warmup (connection setup), so it is skipped
  if (self->ping_sent > 1)
    g_array_append_val (self->rtts, rtt);

  ping_next (self);
  g_object_unref (self);
}

static void
ping_next (VelocitySpeedtest *self)
{
  PingCtx *p;
  g_autofree char *url = NULL;
  SoupMessage *msg;

  if (self->ping_sent >= PING_SAMPLES + 1)
    {
      ping_finish (self);
      return;
    }

  p = g_new0 (PingCtx, 1);
  url = velocity_speedtest_build_url (self, self->ping_path, NULL);
  msg = soup_message_new (SOUP_METHOD_GET, url);

  p->self = g_object_ref (self);
  p->generation = self->ping_generation;
  p->t0 = g_get_monotonic_time ();
  self->ping_sent++;

  soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                    self->run_cancellable, ping_done_cb, p);
  g_object_unref (msg);
}

void
velocity_speedtest_start_ping (VelocitySpeedtest *self)
{
  g_array_set_size (self->rtts, 0);
  self->ping_sent = 0;
  self->ping_generation++;

  // ping runs in the background, the download starts right away
  ping_next (self);
  velocity_speedtest_start_transfer (self, VELOCITY_PHASE_DOWNLOAD);
}
