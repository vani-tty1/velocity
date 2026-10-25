/* velocity-speedtest-transfer.c
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

#define DL_STREAMS        4
#define UL_STREAMS        3
#define TEST_SECONDS      10.0
#define WARMUP_SECONDS    1.5
#define TICK_MS           200
#define DL_CHUNK_MB       100
#define UL_BLOB_BYTES     (16 * 1024 * 1024)
#define READ_BUF_BYTES    (64 * 1024)

static void finish_transfer (VelocitySpeedtest *self);

typedef struct {
  VelocitySpeedtest *self;
  guint              generation;
  VelocityPhase      phase;
  GCancellable      *cancellable;
  SoupSession       *session;
  SoupMessage       *msg;
  GInputStream      *in;
  guchar            *buf;
} Stream;

static void stream_start (Stream *s);

static void
stream_free (Stream *s)
{
  VelocitySpeedtest *self = s->self;
  gboolean current = (s->generation == self->generation);

  if (s->in != NULL)
    g_input_stream_close (s->in, NULL, NULL);

  g_clear_object (&s->in);
  g_clear_object (&s->msg);
  g_clear_object (&s->cancellable);
  g_clear_object (&s->session);
  g_free (s->buf);

  if (current)
    self->active_streams--;

  g_free (s);

  if (current && self->active_streams == 0 && self->tick_id != 0)
    {
      if (self->bytes > 0)
        finish_transfer (self);
      else
        velocity_speedtest_fail (self, "The speed test server refused the connection.");
    }

  g_object_unref (self);
}

static gboolean
stream_stale (Stream *s)
{
  return s->generation != s->self->generation ||
         g_cancellable_is_cancelled (s->cancellable);
}

static void
stream_restart (Stream *s)
{
  // Keep the phase saturated, when one request completes, start another.
  g_clear_object (&s->in);
  g_clear_object (&s->msg);

  if (stream_stale (s))
    {
      stream_free (s);
      return;
    }

  stream_start (s);
}

static void
dl_read_cb (GObject      *source,
            GAsyncResult *res,
            gpointer      user_data)
{
  Stream *s = user_data;
  g_autoptr(GError) error = NULL;
  gssize n;

  n = g_input_stream_read_finish (G_INPUT_STREAM (source), res, &error);

  if (stream_stale (s) || n < 0)
    {
      stream_free (s);
      return;
    }

  if (n == 0)
    {
      stream_restart (s);
      return;
    }

  s->self->bytes += n;

  g_input_stream_read_async (s->in, s->buf, READ_BUF_BYTES, G_PRIORITY_DEFAULT,
                             s->cancellable, dl_read_cb, s);
}

static void
dl_sent_cb (GObject      *source,
            GAsyncResult *res,
            gpointer      user_data)
{
  Stream *s = user_data;
  g_autoptr(GError) error = NULL;

  s->in = soup_session_send_finish (SOUP_SESSION (source), res, &error);

  if (s->in == NULL || stream_stale (s) ||
      soup_message_get_status (s->msg) != SOUP_STATUS_OK)
    {
      stream_free (s);
      return;
    }

  g_input_stream_read_async (s->in, s->buf, READ_BUF_BYTES, G_PRIORITY_DEFAULT,
                             s->cancellable, dl_read_cb, s);
}

static void
ul_wrote_cb (SoupMessage *msg,
             guint        chunk_size,
             gpointer     user_data)
{
  Stream *s = user_data;

  if (s->generation == s->self->generation)
    s->self->bytes += chunk_size;
}

static void
ul_done_cb (GObject      *source,
            GAsyncResult *res,
            gpointer      user_data)
{
  Stream *s = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) body = NULL;

  body = soup_session_send_and_read_finish (SOUP_SESSION (source), res, &error);

  if (body == NULL || stream_stale (s))
    {
      stream_free (s);
      return;
    }

  stream_restart (s);
}

static void
stream_start (Stream *s)
{
  VelocitySpeedtest *self = s->self;
  g_autofree char *url = NULL;

  if (s->phase == VELOCITY_PHASE_DOWNLOAD)
    {
      g_autofree char *q = g_strdup_printf ("ckSize=%d&cors=true", DL_CHUNK_MB);

      url = velocity_speedtest_build_url (self, self->dl_path, q);
      s->msg = soup_message_new (SOUP_METHOD_GET, url);
      soup_session_send_async (s->session, s->msg, G_PRIORITY_DEFAULT,
                               s->cancellable, dl_sent_cb, s);
    }
  else
    {
      url = velocity_speedtest_build_url (self, self->ul_path, NULL);
      s->msg = soup_message_new (SOUP_METHOD_POST, url);
      soup_message_set_request_body_from_bytes (s->msg,
                                                "application/octet-stream",
                                                self->upload_blob);
      g_signal_connect (s->msg, "wrote-body-data", G_CALLBACK (ul_wrote_cb), s);
      soup_session_send_and_read_async (s->session, s->msg,
                                        G_PRIORITY_DEFAULT, s->cancellable,
                                        ul_done_cb, s);
    }
}

static void
finish_transfer (VelocitySpeedtest *self)
{
  gint64 now = g_get_monotonic_time ();
  gint64 t0 = self->warm_set ? self->t_warm : self->t_start;
  gint64 b0 = self->warm_set ? self->bytes_warm : 0;
  double secs = (now - t0) / 1e6;
  double mbps = secs > 0 ? ((self->bytes - b0) * 8.0) / secs / 1e6 : 0.0;
  VelocityPhase phase = self->phase;

  velocity_speedtest_stop_tick (self);
  if (self->phase_cancellable != NULL)
    {
      g_cancellable_cancel (self->phase_cancellable);
      g_clear_object (&self->phase_cancellable);
    }

  if (self->bytes == 0)
    {
      velocity_speedtest_fail (self, "No data was transferred.");
      return;
    }

  self->generation++;
  self->active_streams = 0;

  velocity_speedtest_emit_progress (self, phase, mbps);

  if (phase == VELOCITY_PHASE_DOWNLOAD)
    {
      self->download = mbps;
      velocity_speedtest_start_transfer (self, VELOCITY_PHASE_UPLOAD);
    }
  else
    {
      self->upload = mbps;
      velocity_speedtest_set_phase (self, VELOCITY_PHASE_DONE);
      velocity_speedtest_emit_finished (self);
    }
}

static gboolean
tick_cb (gpointer user_data)
{
  VelocitySpeedtest *self = user_data;
  gint64 now = g_get_monotonic_time ();
  double elapsed = (now - self->t_start) / 1e6;
  double window = (now - self->t_last) / 1e6;
  double inst;

  if (window > 0)
    {
      inst = ((self->bytes - self->bytes_last) * 8.0) / window / 1e6;
      self->smoothed = self->smoothed == 0.0 ? inst
                                             : 0.6 * self->smoothed + 0.4 * inst;
    }
  self->bytes_last = self->bytes;
  self->t_last = now;

  if (!self->warm_set && elapsed >= WARMUP_SECONDS)
    {
      self->warm_set = TRUE;
      self->t_warm = now;
      self->bytes_warm = self->bytes;
    }

  velocity_speedtest_emit_progress (self, self->phase, self->smoothed);

  if (elapsed >= TEST_SECONDS)
    {
      // remove source by returning FALSE
      self->tick_id = 0;
      finish_transfer (self);
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

void
velocity_speedtest_start_transfer (VelocitySpeedtest *self,
                                   VelocityPhase      phase)
{
  int n = phase == VELOCITY_PHASE_DOWNLOAD ? DL_STREAMS : UL_STREAMS;

  velocity_speedtest_set_phase (self, phase);

  self->bytes = 0;
  self->bytes_last = 0;
  self->bytes_warm = 0;
  self->warm_set = FALSE;
  self->smoothed = 0.0;
  self->t_start = self->t_last = g_get_monotonic_time ();
  self->active_streams = 0;
  g_clear_object (&self->phase_cancellable);
  self->phase_cancellable = g_cancellable_new ();

  if (phase == VELOCITY_PHASE_UPLOAD && self->upload_blob == NULL)
    {
      guint32 *data = g_malloc (UL_BLOB_BYTES);

      for (gsize i = 0; i < UL_BLOB_BYTES / sizeof (guint32); i++)
        data[i] = g_random_int ();
      self->upload_blob = g_bytes_new_take (data, UL_BLOB_BYTES);
    }

  self->tick_id = g_timeout_add_full (G_PRIORITY_DEFAULT, TICK_MS, tick_cb,
                                      g_object_ref (self), g_object_unref);

  for (int i = 0; i < n; i++)
    {
      Stream *s = g_new0 (Stream, 1);

      s->self = g_object_ref (self);
      s->generation = self->generation;
      s->phase = phase;
      s->cancellable = g_object_ref (self->phase_cancellable);
      s->session = soup_session_new_with_options ("user-agent",
                                                  "Velocity/" PACKAGE_VERSION,
                                                  "timeout", 15,
                                                  NULL);
      s->buf = phase == VELOCITY_PHASE_DOWNLOAD ? g_malloc (READ_BUF_BYTES) : NULL;
      self->active_streams++;
      stream_start (s);
    }
}
