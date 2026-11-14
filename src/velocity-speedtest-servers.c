/* velocity-speedtest-servers.c
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

#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "velocity-speedtest-private.h"

#ifndef LIBRESPEED_LIST_URL
#define LIBRESPEED_LIST_URL "https://librespeed.org/backend-servers/servers.php"
#endif
#ifndef OOKLA_LIST_URL
#define OOKLA_LIST_URL "https://www.speedtest.net/api/js/servers?engine=js&limit=100"
#endif
#ifndef OOKLA_SCHEME
#define OOKLA_SCHEME "https"
#endif

#define MAX_LIBRESPEED_CANDIDATES 10
#define MAX_OOKLA_CANDIDATES 10

// how long a background-probed best server is trusted, and how many probes
// run at once (more than the session's connection limit would only queue,
// and queueing time would count as latency)
#define BEST_SERVER_TTL_SECONDS (30)
#define NETWORK_SETTLE_SECONDS 2
#define PRELOAD_CONCURRENCY 8

void
velocity_speedtest_candidate_free (Candidate *c)
{
  g_free (c->name);
  g_free (c->base);
  g_free (c->dl_path);
  g_free (c->ul_path);
  g_free (c->ping_path);
  g_free (c);
}

static char *
normalize_base (const char *server)
{
  g_autofree char *tmp = NULL;

  if (server == NULL || *server == '\0')
    return NULL;

  if (g_str_has_prefix (server, "//"))
    tmp = g_strconcat ("https:", server, NULL);
  else if (g_str_has_prefix (server, "https://"))
    tmp = g_strdup (server);
  else
    return NULL;

  if (g_str_has_suffix (tmp, "/"))
    return g_steal_pointer (&tmp);

  return g_strconcat (tmp, "/", NULL);
}

static const char *
backend_label (VelocityBackend backend)
{
  return backend == VELOCITY_BACKEND_OOKLA ? "Ookla" : "LibreSpeed";
}

static Candidate *
candidate_new (VelocityBackend backend,
               const char *name,
               char *base,
               guint rank)
{
  Candidate *c = g_new0 (Candidate, 1);

  c->backend = backend;
  c->name = g_strdup_printf ("%s · %s", name, backend_label (backend));
  c->base = base;
  c->rank = rank;

  if (backend == VELOCITY_BACKEND_OOKLA)
    {
      c->dl_path = g_strdup ("random4000x4000.jpg");
      c->ul_path = g_strdup ("upload.php");
      c->ping_path = g_strdup ("latency.txt");
    }
  else
    {
      c->dl_path = g_strdup ("garbage.php");
      c->ul_path = g_strdup ("empty.php");
      c->ping_path = g_strdup ("empty.php");
    }

  return c;
}

static Candidate *
find_by_base (VelocitySpeedtest *self,
              const char *base)
{
  for (guint i = 0; base != NULL && i < self->servers->len; i++)
    {
      Candidate *c = g_ptr_array_index (self->servers, i);

      if (g_strcmp0 (c->base, base) == 0)
        return c;
    }

  return NULL;
}

typedef struct
{
  VelocitySpeedtest *self;
  guint generation;
  Candidate *candidate;
  gint64 t0;
} ProbeCtx;

static void
use_candidate (VelocitySpeedtest *self,
               Candidate *c)
{
  g_free (self->server_name);
  g_free (self->base);
  g_free (self->dl_path);
  g_free (self->ul_path);
  g_free (self->ping_path);

  self->backend = c->backend;
  self->server_name = g_strdup (c->name);
  self->base = g_strdup (c->base);
  self->dl_path = g_strdup (c->dl_path);
  self->ul_path = g_strdup (c->ul_path);
  self->ping_path = g_strdup (c->ping_path);
}

static Candidate *
candidate_dup (const Candidate *c)
{
  Candidate *copy = g_new0 (Candidate, 1);

  copy->name = g_strdup (c->name);
  copy->base = g_strdup (c->base);
  copy->dl_path = g_strdup (c->dl_path);
  copy->ul_path = g_strdup (c->ul_path);
  copy->ping_path = g_strdup (c->ping_path);
  copy->backend = c->backend;
  copy->rank = c->rank;

  return copy;
}

static gboolean
best_server_is_fresh (VelocitySpeedtest *self)
{
  return self->best_cached != NULL &&
         g_get_monotonic_time () - self->best_cached_at <
             (gint64) BEST_SERVER_TTL_SECONDS * G_USEC_PER_SEC;
}

static gboolean
test_is_idle (VelocitySpeedtest *self)
{
  return self->phase == VELOCITY_PHASE_IDLE ||
         self->phase == VELOCITY_PHASE_DONE ||
         self->phase == VELOCITY_PHASE_FAILED;
}

// Probing only makes sense in automatic mode, between tests, with servers known
static gboolean
preload_wanted (VelocitySpeedtest *self)
{
  const char *override = g_getenv ("VELOCITY_SERVER_URL");

  return test_is_idle (self) &&
         (override == NULL || *override == '\0') &&
         find_by_base (self, self->preferred_base) == NULL &&
         self->candidates->len > 0;
}

typedef struct
{
  VelocitySpeedtest *self;
  guint generation;
  Candidate *candidate;
  gint64 t0;
} PreloadCtx;

static void preload_launch_next (VelocitySpeedtest *self);

void
velocity_speedtest_cancel_preload (VelocitySpeedtest *self)
{
  gboolean was_running = self->preload_cancellable != NULL;

  self->preload_generation++;
  self->preload_best = NULL;
  self->preload_next = 0;
  self->preload_pending = 0;

  if (was_running)
    {
      g_cancellable_cancel (self->preload_cancellable);
      g_clear_object (&self->preload_cancellable);
      velocity_speedtest_emit_preload_changed (self);
    }
}

void
velocity_speedtest_invalidate_best_server (VelocitySpeedtest *self)
{
  g_clear_pointer (&self->best_cached, velocity_speedtest_candidate_free);
}

static gboolean
preload_timeout_cb (gpointer user_data)
{
  VelocitySpeedtest *self = user_data;

  self->preload_timeout_id = 0;
  velocity_speedtest_preload_best_server (self);

  return G_SOURCE_REMOVE;
}

static void
schedule_preload (VelocitySpeedtest *self,
                  guint seconds)
{
  if (self->preload_timeout_id != 0)
    g_source_remove (self->preload_timeout_id);

  // +1: a seconds timer may fire up to a second early, before the cache expired
  self->preload_timeout_id = g_timeout_add_seconds (seconds + 1, preload_timeout_cb, self);
}

static void
preload_finished (VelocitySpeedtest *self)
{
  Candidate *best = self->preload_best;
  gboolean waiting = self->waiting_for_preload;

  g_clear_object (&self->preload_cancellable);
  self->preload_best = NULL;
  self->waiting_for_preload = FALSE;

  if (best != NULL)
    {
      g_clear_pointer (&self->best_cached, velocity_speedtest_candidate_free);
      self->best_cached = candidate_dup (best);
      self->best_cached_at = g_get_monotonic_time ();

      // keep it fresh for as long as the app stays open
      schedule_preload (self, BEST_SERVER_TTL_SECONDS);
      velocity_speedtest_emit_best_server_changed (self);
    }

  velocity_speedtest_emit_preload_changed (self);

  if (!waiting)
    return;

  if (best == NULL)
    {
      velocity_speedtest_fail (self, "Could not reach any speed test server.");
      return;
    }
  use_candidate (self, best);
  velocity_speedtest_start_ping (self);
}

static void
preload_done_cb (GObject *source,
                 GAsyncResult *res,
                 gpointer user_data)
{
  PreloadCtx *p = user_data;
  VelocitySpeedtest *self = p->self;
  g_autoptr (GError) error = NULL;
  g_autoptr (GBytes) body = NULL;

  body = soup_session_send_and_read_finish (SOUP_SESSION (source), res, &error);

  if (p->generation == self->preload_generation)
    {
      if (body != NULL)
        {
          double rtt = (g_get_monotonic_time () - p->t0) / 1000.0;

          if (self->preload_best == NULL || rtt < self->preload_best_rtt)
            {
              self->preload_best = p->candidate;
              self->preload_best_rtt = rtt;
            }
        }

      if (self->preload_next < self->candidates->len)
        preload_launch_next (self);

      if (--self->preload_pending == 0)
        preload_finished (self);
    }

  g_free (p);
  g_object_unref (self);
}

static void
preload_launch_next (VelocitySpeedtest *self)
{
  Candidate *c = g_ptr_array_index (self->candidates, self->preload_next++);
  PreloadCtx *p = g_new0 (PreloadCtx, 1);
  g_autofree char *url = g_strdup_printf ("%s%s?r=%u", c->base, c->ping_path,
                                          g_random_int ());
  SoupMessage *msg = soup_message_new (SOUP_METHOD_GET, url);

  p->self = g_object_ref (self);
  p->generation = self->preload_generation;
  p->candidate = c;
  p->t0 = g_get_monotonic_time ();

  soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                    self->preload_cancellable, preload_done_cb, p);
  g_object_unref (msg);
}

static void
start_preload (VelocitySpeedtest *self)
{
  guint n;

  velocity_speedtest_cancel_preload (self);

  self->preload_cancellable = g_cancellable_new ();
  self->preload_pending = (int) self->candidates->len;
  n = MIN ((guint) PRELOAD_CONCURRENCY, self->candidates->len);

  for (guint i = 0; i < n; i++)
    preload_launch_next (self);

  velocity_speedtest_emit_preload_changed (self);
}

void
velocity_speedtest_preload_best_server (VelocitySpeedtest *self)
{
  if (self->preload_cancellable != NULL || best_server_is_fresh (self))
    return;

  if (preload_wanted (self))
    start_preload (self);
}

static void
on_network_changed (GNetworkMonitor *monitor,
                    gboolean available,
                    VelocitySpeedtest *self)
{
  // latency measured on the old network says nothing about the new one,
  // a running test does its own thing, so only act between tests
  if (!test_is_idle (self))
    return;

  velocity_speedtest_cancel_preload (self);
  velocity_speedtest_invalidate_best_server (self);

  if (available)
    schedule_preload (self, NETWORK_SETTLE_SECONDS);
}

void
velocity_speedtest_watch_network (VelocitySpeedtest *self)
{
  g_signal_connect_object (g_network_monitor_get_default (), "network-changed",
                           G_CALLBACK (on_network_changed), self, 0);
}

const char *
velocity_speedtest_get_best_server (VelocitySpeedtest *self)
{
  g_return_val_if_fail (VELOCITY_IS_SPEEDTEST (self), NULL);

  return best_server_is_fresh (self) ? self->best_cached->name : NULL;
}

gboolean
velocity_speedtest_is_preloading (VelocitySpeedtest *self)
{
  g_return_val_if_fail (VELOCITY_IS_SPEEDTEST (self), FALSE);

  return self->preload_cancellable != NULL;
}

// (name, backend, base, dl path, ul path, ping path, Unix time measured)
GVariant *
velocity_speedtest_get_best_server_state (VelocitySpeedtest *self)
{
  Candidate *c = self->best_cached;
  gint64 age;

  g_return_val_if_fail (VELOCITY_IS_SPEEDTEST (self), NULL);

  if (!best_server_is_fresh (self))
    return NULL;

  age = (g_get_monotonic_time () - self->best_cached_at) / G_USEC_PER_SEC;

  return g_variant_new ("(sissssx)", c->name, (gint32) c->backend, c->base,
                        c->dl_path, c->ul_path, c->ping_path,
                        g_get_real_time () / G_USEC_PER_SEC - age);
}

// paths come from a settings file, so only accept plain relative ones
static gboolean
valid_cached_path (const char *path)
{
  return path != NULL && *path != '\0' && path[0] != '/' &&
         strstr (path, "://") == NULL && strchr (path, ' ') == NULL;
}

void
velocity_speedtest_set_best_server_state (VelocitySpeedtest *self,
                                          GVariant *state)
{
  const char *name, *base, *dl, *ul, *ping;
  g_autofree char *checked_base = NULL;
  gint32 backend;
  gint64 saved, age;
  Candidate *c;

  g_return_if_fail (VELOCITY_IS_SPEEDTEST (self));

  if (state == NULL || !g_variant_is_of_type (state, G_VARIANT_TYPE ("(sissssx)")) ||
      best_server_is_fresh (self))
    return;

  g_variant_get (state, "(&si&s&s&s&sx)", &name, &backend, &base, &dl, &ul, &ping, &saved);

  age = g_get_real_time () / G_USEC_PER_SEC - saved;
  checked_base = normalize_base (base);

  if (age < 0 || age >= BEST_SERVER_TTL_SECONDS ||
      backend < 0 || backend >= VELOCITY_N_BACKENDS ||
      *name == '\0' || g_strcmp0 (checked_base, base) != 0 ||
      !valid_cached_path (dl) || !valid_cached_path (ul) || !valid_cached_path (ping))
    return;

  c = g_new0 (Candidate, 1);
  c->name = g_strdup (name);
  c->base = g_strdup (base);
  c->dl_path = g_strdup (dl);
  c->ul_path = g_strdup (ul);
  c->ping_path = g_strdup (ping);
  c->backend = (VelocityBackend) backend;

  g_clear_pointer (&self->best_cached, velocity_speedtest_candidate_free);
  self->best_cached = c;
  self->best_cached_at = g_get_monotonic_time () - age * G_USEC_PER_SEC;

  // refresh it once it expires
  schedule_preload (self, (guint) (BEST_SERVER_TTL_SECONDS - age));
}

static void
probe_done_cb (GObject *source,
               GAsyncResult *res,
               gpointer user_data)
{
  ProbeCtx *p = user_data;
  VelocitySpeedtest *self = p->self;
  g_autoptr (GError) error = NULL;
  g_autoptr (GBytes) body = NULL;
  gboolean stale = (p->generation != self->generation);

  body = soup_session_send_and_read_finish (SOUP_SESSION (source), res, &error);

  if (stale)
    goto out;

  if (body != NULL)
    p->candidate->rtt = (g_get_monotonic_time () - p->t0) / 1000.0;

  if (--self->probes_pending == 0)
    {
      Candidate *best = NULL;

      for (guint i = 0; i < self->candidates->len; i++)
        {
          Candidate *c = g_ptr_array_index (self->candidates, i);

          if (c->rtt > 0 && (best == NULL || c->rtt < best->rtt))
            best = c;
        }

      if (best == NULL)
        {
          velocity_speedtest_fail (self, "Could not reach any speed test server.");
        }
      else
        {
          use_candidate (self, best);
          velocity_speedtest_start_ping (self);
        }
    }

out:
  g_free (p);
  g_object_unref (self);
}

static void
probe_candidates (VelocitySpeedtest *self)
{
  if (self->candidates->len == 0)
    {
      velocity_speedtest_fail (self, "No speed test servers are available.");
      return;
    }

  self->probes_pending = (int) self->candidates->len;

  for (guint i = 0; i < self->candidates->len; i++)
    {
      Candidate *c = g_ptr_array_index (self->candidates, i);
      ProbeCtx *p = g_new0 (ProbeCtx, 1);
      g_autofree char *url = g_strdup_printf ("%s%s?r=%u", c->base, c->ping_path,
                                              g_random_int ());
      SoupMessage *msg = soup_message_new (SOUP_METHOD_GET, url);

      c->rtt = 0;
      p->self = g_object_ref (self);
      p->generation = self->generation;
      p->candidate = c;
      p->t0 = g_get_monotonic_time ();

      soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                        self->run_cancellable, probe_done_cb, p);
      g_object_unref (msg);
    }
}

static int
compare_by_name (gconstpointer a,
                 gconstpointer b)
{
  const Candidate *x = *(Candidate *const *) a;
  const Candidate *y = *(Candidate *const *) b;

  return g_utf8_collate (x->name, y->name);
}

/* The automatic latency probe only covers the best few servers of each list,
 * so rebuild that subset whenever a list arrives. */
static void
rebuild_candidates (VelocitySpeedtest *self)
{
  g_ptr_array_set_size (self->candidates, 0);

  for (guint i = 0; i < self->servers->len; i++)
    {
      Candidate *c = g_ptr_array_index (self->servers, i);
      guint max = c->backend == VELOCITY_BACKEND_OOKLA ? MAX_OOKLA_CANDIDATES
                                                       : MAX_LIBRESPEED_CANDIDATES;

      if (c->rank < max)
        g_ptr_array_add (self->candidates, c);
    }
}

static JsonArray *
load_json_array (GBytes *bytes,
                 JsonParser *parser)
{
  g_autoptr (GError) error = NULL;
  gsize len;
  const char *data = g_bytes_get_data (bytes, &len);

  if (!json_parser_load_from_data (parser, data, (gssize) len, &error) ||
      !JSON_NODE_HOLDS_ARRAY (json_parser_get_root (parser)))
    return NULL;

  return json_node_get_array (json_parser_get_root (parser));
}

// Ookla hands out "host:port"; only accept what can safely go into a URL
static gboolean
valid_ookla_host (const char *host)
{
  gsize len;

  if (host == NULL || *host == '\0')
    return FALSE;

  len = strlen (host);
  if (len > 253 || host[0] == '.' || host[0] == '-' || strstr (host, "..") != NULL)
    return FALSE;

  for (gsize i = 0; i < len; i++)
    {
      if (!g_ascii_isalnum (host[i]) && host[i] != '.' && host[i] != '-' && host[i] != ':')
        return FALSE;
    }

  return TRUE;
}

static const char *
parse_librespeed_list (VelocitySpeedtest *self,
                       GBytes *bytes)
{
  g_autoptr (JsonParser) parser = json_parser_new ();
  JsonArray *arr = load_json_array (bytes, parser);
  guint added = 0;

  if (arr == NULL)
    return "The LibreSpeed server list could not be read.";

  for (guint i = 0; i < json_array_get_length (arr); i++)
    {
      JsonObject *o = json_array_get_object_element (arr, i);
      g_autofree char *base = NULL;
      Candidate *c;

      if (o == NULL)
        continue;

      base = normalize_base (json_object_get_string_member_with_default (o, "server", NULL));
      if (base == NULL || find_by_base (self, base) != NULL)
        continue;

      c = candidate_new (VELOCITY_BACKEND_LIBRESPEED,
                         json_object_get_string_member_with_default (o, "name", base),
                         g_steal_pointer (&base), added++);
      g_free (c->dl_path);
      g_free (c->ul_path);
      g_free (c->ping_path);
      c->dl_path = g_strdup (json_object_get_string_member_with_default (o, "dlURL", "garbage.php"));
      c->ul_path = g_strdup (json_object_get_string_member_with_default (o, "ulURL", "empty.php"));
      c->ping_path = g_strdup (json_object_get_string_member_with_default (o, "pingURL", "empty.php"));
      g_ptr_array_add (self->servers, c);
    }

  if (added == 0)
    return "The LibreSpeed server list was empty.";

  return NULL;
}

static const char *
parse_ookla_list (VelocitySpeedtest *self,
                  GBytes *bytes)
{
  g_autoptr (JsonParser) parser = json_parser_new ();
  JsonArray *arr = load_json_array (bytes, parser);
  guint added = 0;

  if (arr == NULL)
    return "The Ookla server list could not be read.";

  for (guint i = 0; i < json_array_get_length (arr); i++)
    {
      JsonObject *o = json_array_get_object_element (arr, i);
      const char *host, *city, *sponsor;
      g_autofree char *base = NULL;
      g_autofree char *name = NULL;

      if (o == NULL)
        continue;

      host = json_object_get_string_member_with_default (o, "host", NULL);
      if (!valid_ookla_host (host))
        continue;

      base = g_strdup_printf ("%s://%s/speedtest/", OOKLA_SCHEME, host);
      if (find_by_base (self, base) != NULL)
        continue;

      city = json_object_get_string_member_with_default (o, "name", host);
      sponsor = json_object_get_string_member_with_default (o, "sponsor", "");
      name = *sponsor != '\0' ? g_strdup_printf ("%s (%s)", city, sponsor) : g_strdup (city);

      g_ptr_array_add (self->servers,
                       candidate_new (VELOCITY_BACKEND_OOKLA, name,
                                      g_steal_pointer (&base), added++));
    }

  if (added == 0)
    return "The Ookla server list was empty.";

  return NULL;
}

static const char *
parse_server_list (VelocitySpeedtest *self,
                   VelocityBackend backend,
                   GBytes *bytes)
{
  const char *failure = backend == VELOCITY_BACKEND_OOKLA
                            ? parse_ookla_list (self, bytes)
                            : parse_librespeed_list (self, bytes);

  if (failure != NULL)
    return failure;

  self->loaded[backend] = TRUE;
  g_ptr_array_sort (self->servers, compare_by_name);
  rebuild_candidates (self);
  velocity_speedtest_emit_servers_changed (self);

  // the candidate set just changed, so any earlier probe is out of date
  // (unless the cached result is still good, e.g. restored from last time)
  if (!best_server_is_fresh (self) && preload_wanted (self))
    start_preload (self);

  return NULL;
}

static void
choose_server (VelocitySpeedtest *self)
{
  Candidate *preferred = find_by_base (self, self->preferred_base);

  if (preferred != NULL)
    {
      use_candidate (self, preferred);
      velocity_speedtest_start_ping (self);
      return;
    }

  // already probed in the background: skip the lookup entirely
  if (best_server_is_fresh (self))
    {
      // a refresh still in flight would compete with the latency test
      velocity_speedtest_cancel_preload (self);
      use_candidate (self, self->best_cached);
      velocity_speedtest_start_ping (self);
      return;
    }

  if (self->phase != VELOCITY_PHASE_SERVER)
    velocity_speedtest_set_phase (self, VELOCITY_PHASE_SERVER);

  // wait for it instead of starting a
  // second one (preload_finished carries on with the test)
  if (self->preload_cancellable != NULL)
    {
      self->waiting_for_preload = TRUE;
      return;
    }

  probe_candidates (self);
}

typedef struct
{
  VelocitySpeedtest *self;
  VelocityBackend backend;
  gboolean for_run;
  guint generation;
} ListCtx;

static void
list_done_cb (GObject *source,
              GAsyncResult *res,
              gpointer user_data)
{
  ListCtx *l = user_data;
  VelocitySpeedtest *self = l->self;
  g_autoptr (GBytes) bytes = soup_session_send_and_read_finish (SOUP_SESSION (source), res, NULL);
  gboolean stale = l->for_run && l->generation != self->generation;

  // either list may arrive first, and one failing must not block the other
  if (bytes != NULL && !self->loaded[l->backend])
    parse_server_list (self, l->backend, bytes);

  if (l->for_run && !stale && --self->lists_pending == 0)
    {
      if (self->candidates->len == 0 && find_by_base (self, self->preferred_base) == NULL)
        velocity_speedtest_fail (self, "Could not download the server list. Check your connection.");
      else
        choose_server (self);
    }

  g_free (l);
  g_object_unref (self);
}

static void
fetch_list (VelocitySpeedtest *self,
            VelocityBackend backend,
            gboolean for_run)
{
  g_autoptr (SoupMessage) msg = NULL;
  ListCtx *l = g_new0 (ListCtx, 1);

  msg = soup_message_new (SOUP_METHOD_GET, backend == VELOCITY_BACKEND_OOKLA
                                               ? OOKLA_LIST_URL
                                               : LIBRESPEED_LIST_URL);
  l->self = g_object_ref (self);
  l->backend = backend;
  l->for_run = for_run;
  l->generation = self->generation;

  soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                    for_run ? self->run_cancellable : NULL,
                                    list_done_cb, l);
}

void
velocity_speedtest_load_servers (VelocitySpeedtest *self)
{
  g_return_if_fail (VELOCITY_IS_SPEEDTEST (self));

  for (int b = 0; b < VELOCITY_N_BACKENDS; b++)
    {
      if (!self->loaded[b])
        fetch_list (self, b, FALSE);
    }
}

guint
velocity_speedtest_get_n_servers (VelocitySpeedtest *self)
{
  g_return_val_if_fail (VELOCITY_IS_SPEEDTEST (self), 0);

  return self->servers->len;
}

const char *
velocity_speedtest_get_server_name_at (VelocitySpeedtest *self,
                                       guint index)
{
  Candidate *c;

  g_return_val_if_fail (VELOCITY_IS_SPEEDTEST (self), NULL);
  g_return_val_if_fail (index < self->servers->len, NULL);

  c = g_ptr_array_index (self->servers, index);
  return c->name;
}

const char *
velocity_speedtest_get_server_base_at (VelocitySpeedtest *self,
                                       guint index)
{
  Candidate *c;

  g_return_val_if_fail (VELOCITY_IS_SPEEDTEST (self), NULL);
  g_return_val_if_fail (index < self->servers->len, NULL);

  c = g_ptr_array_index (self->servers, index);
  return c->base;
}

void
velocity_speedtest_select_server (VelocitySpeedtest *self)
{
  const char *override;
  gboolean all_loaded = TRUE;

  // VELOCITY_SERVER_URL=http://host:port/path/ [VELOCITY_SERVER_TYPE=librespeed|ookla]
  override = g_getenv ("VELOCITY_SERVER_URL");
  if (override != NULL && *override != '\0')
    {
      const char *type = g_getenv ("VELOCITY_SERVER_TYPE");
      VelocityBackend backend = g_strcmp0 (type, "ookla") == 0 ? VELOCITY_BACKEND_OOKLA
                                                               : VELOCITY_BACKEND_LIBRESPEED;
      Candidate *c = candidate_new (backend, override,
                                    g_str_has_suffix (override, "/") ? g_strdup (override)
                                                                     : g_strconcat (override, "/", NULL),
                                    0);

      use_candidate (self, c);
      velocity_speedtest_candidate_free (c);
      velocity_speedtest_start_ping (self);
      return;
    }

  for (int b = 0; b < VELOCITY_N_BACKENDS; b++)
    all_loaded = all_loaded && self->loaded[b];

  // a server picked in the UI doesn't need the other lists, or any list at all,
  // and neither does a best server that was already probed
  if (all_loaded || find_by_base (self, self->preferred_base) != NULL ||
      (self->preferred_base == NULL && best_server_is_fresh (self)))
    {
      choose_server (self);
      return;
    }

  velocity_speedtest_set_phase (self, VELOCITY_PHASE_SERVER);

  self->lists_pending = 0;
  for (int b = 0; b < VELOCITY_N_BACKENDS; b++)
    {
      if (!self->loaded[b])
        self->lists_pending++;
    }
  for (int b = 0; b < VELOCITY_N_BACKENDS; b++)
    {
      if (!self->loaded[b])
        fetch_list (self, b, TRUE);
    }
}
