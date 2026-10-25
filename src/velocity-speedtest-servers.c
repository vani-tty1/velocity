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

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "velocity-speedtest-private.h"

#define SERVER_LIST_URL   "https://librespeed.org/backend-servers/servers.php"
#define MAX_CANDIDATES    12

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

typedef struct {
  VelocitySpeedtest *self;
  guint             generation;
  Candidate        *candidate;
  gint64            t0;
} ProbeCtx;

static void
use_candidate (VelocitySpeedtest *self,
               Candidate        *c)
{
  g_free (self->server_name);
  g_free (self->base);
  g_free (self->dl_path);
  g_free (self->ul_path);
  g_free (self->ping_path);

  self->server_name = g_strdup (c->name);
  self->base = g_strdup (c->base);
  self->dl_path = g_strdup (c->dl_path);
  self->ul_path = g_strdup (c->ul_path);
  self->ping_path = g_strdup (c->ping_path);
}

static void
probe_done_cb (GObject      *source,
               GAsyncResult *res,
               gpointer      user_data)
{
  ProbeCtx *p = user_data;
  VelocitySpeedtest *self = p->self;
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) body = NULL;
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
  self->probes_pending = (int) self->candidates->len;

  for (guint i = 0; i < self->candidates->len; i++)
    {
      Candidate *c = g_ptr_array_index (self->candidates, i);
      ProbeCtx *p = g_new0 (ProbeCtx, 1);
      g_autofree char *url = g_strdup_printf ("%s%s?r=%u", c->base, c->ping_path,
                                              g_random_int ());
      SoupMessage *msg = soup_message_new (SOUP_METHOD_GET, url);

      p->self = g_object_ref (self);
      p->generation = self->generation;
      p->candidate = c;
      p->t0 = g_get_monotonic_time ();

      soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                        self->run_cancellable, probe_done_cb, p);
      g_object_unref (msg);
    }
}

static void
parse_server_list (VelocitySpeedtest *self,
                   GBytes           *bytes)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  g_autoptr(GError) error = NULL;
  JsonArray *arr;
  gsize len;
  const char *data = g_bytes_get_data (bytes, &len);

  if (!json_parser_load_from_data (parser, data, (gssize) len, &error) ||
      !JSON_NODE_HOLDS_ARRAY (json_parser_get_root (parser)))
    {
      velocity_speedtest_fail (self, "The server list could not be read.");
      return;
    }

  arr = json_node_get_array (json_parser_get_root (parser));
  g_ptr_array_set_size (self->candidates, 0);

  for (guint i = 0; i < json_array_get_length (arr) &&
                    self->candidates->len < MAX_CANDIDATES; i++)
    {
      JsonObject *o = json_array_get_object_element (arr, i);
      g_autofree char *base = NULL;
      Candidate *c;

      if (o == NULL)
        continue;

      base = normalize_base (json_object_get_string_member_with_default (o, "server", NULL));
      if (base == NULL)
        continue;

      c = g_new0 (Candidate, 1);
      c->name = g_strdup (json_object_get_string_member_with_default (o, "name", base));
      c->base = g_steal_pointer (&base);
      c->dl_path = g_strdup (json_object_get_string_member_with_default (o, "dlURL", "garbage.php"));
      c->ul_path = g_strdup (json_object_get_string_member_with_default (o, "ulURL", "empty.php"));
      c->ping_path = g_strdup (json_object_get_string_member_with_default (o, "pingURL", "empty.php"));
      g_ptr_array_add (self->candidates, c);
    }

  if (self->candidates->len == 0)
    {
      velocity_speedtest_fail (self, "The server list was empty.");
      return;
    }

  probe_candidates (self);
}

static void
list_done_cb (GObject      *source,
              GAsyncResult *res,
              gpointer      user_data)
{
  VelocitySpeedtest *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) bytes = NULL;
  guint gen = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (self), "list-gen"));

  bytes = soup_session_send_and_read_finish (SOUP_SESSION (source), res, &error);

  if (gen == self->generation)
    {
      if (bytes == NULL)
        velocity_speedtest_fail (self, "Could not download the server list. Check your connection.");
      else
        parse_server_list (self, bytes);
    }

  g_object_unref (self);
}

void
velocity_speedtest_select_server (VelocitySpeedtest *self)
{
  const char *override;
  g_autoptr(SoupMessage) msg = NULL;

  override = g_getenv ("VELOCITY_SERVER_URL");
  if (override != NULL && *override != '\0')
    {
      Candidate c = {
        .name = (char *) override,
        .base = g_str_has_suffix (override, "/") ? g_strdup (override)
                                                 : g_strconcat (override, "/", NULL),
        .dl_path = (char *) "garbage.php",
        .ul_path = (char *) "empty.php",
        .ping_path = (char *) "empty.php",
      };

      use_candidate (self, &c);
      g_free (c.base);
      velocity_speedtest_start_ping (self);
      return;
    }

  velocity_speedtest_set_phase (self, VELOCITY_PHASE_SERVER);

  msg = soup_message_new (SOUP_METHOD_GET, SERVER_LIST_URL);
  g_object_set_data (G_OBJECT (self), "list-gen", GUINT_TO_POINTER (self->generation));
  soup_session_send_and_read_async (self->session, msg, G_PRIORITY_DEFAULT,
                                    self->run_cancellable, list_done_cb,
                                    g_object_ref (self));
}
