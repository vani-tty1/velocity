// Unit tests

#include "../src/velocity-speedtest-servers.c"

#include <libsoup/soup.h>

static void
test_normalize_base (void)
{
  g_autofree char *a = normalize_base ("https://example.com/speedtest");
  g_autofree char *b = normalize_base ("https://example.com/speedtest/");
  g_autofree char *c = normalize_base ("//example.com/");

  g_assert_cmpstr (a, ==, "https://example.com/speedtest/");
  g_assert_cmpstr (b, ==, "https://example.com/speedtest/");
  g_assert_cmpstr (c, ==, "https://example.com/");

  g_assert_null (normalize_base (NULL));
  g_assert_null (normalize_base (""));
  g_assert_null (normalize_base ("http://example.com/"));
  g_assert_null (normalize_base ("ftp://example.com/"));
}

static void
test_valid_ookla_host (void)
{
  g_assert_true (valid_ookla_host ("speed.example.com:8080"));
  g_assert_true (valid_ookla_host ("a-b.example.net"));

  g_assert_false (valid_ookla_host (NULL));
  g_assert_false (valid_ookla_host (""));
  g_assert_false (valid_ookla_host (".example.com"));
  g_assert_false (valid_ookla_host ("-example.com"));
  g_assert_false (valid_ookla_host ("a..b"));
  g_assert_false (valid_ookla_host ("evil.com/path"));
  g_assert_false (valid_ookla_host ("evil.com?x=1"));
  g_assert_false (valid_ookla_host ("a b"));
  g_assert_false (valid_ookla_host ("user@evil.com"));
}

static GBytes *
bytes_from (const char *s)
{
  return g_bytes_new (s, strlen (s));
}

static void
test_parse_ookla_list (void)
{
  g_autoptr (VelocitySpeedtest) self = velocity_speedtest_new ();
  g_autoptr (GBytes) json = bytes_from (
      "[{\"host\":\"one.example.com:8080\",\"name\":\"Manila\",\"sponsor\":\"PLDT\"},"
      " {\"host\":\"one.example.com:8080\",\"name\":\"Manila\",\"sponsor\":\"PLDT\"},"
      " {\"host\":\"evil.com/../x\",\"name\":\"Bad\",\"sponsor\":\"X\"},"
      " {\"host\":\"two.example.com:8080\",\"name\":\"Cebu\",\"sponsor\":\"Globe\"}]");

  g_assert_null (parse_ookla_list (self, json));
  g_assert_cmpuint (velocity_speedtest_get_n_servers (self), ==, 2);
}

static void
test_parse_ookla_rejects_garbage (void)
{
  g_autoptr (VelocitySpeedtest) self = velocity_speedtest_new ();
  g_autoptr (GBytes) not_json = bytes_from ("<html>captive portal</html>");
  g_autoptr (GBytes) not_array = bytes_from ("{\"host\":\"x\"}");
  g_autoptr (GBytes) empty = bytes_from ("[]");

  g_assert_nonnull (parse_ookla_list (self, not_json));
  g_assert_nonnull (parse_ookla_list (self, not_array));
  g_assert_nonnull (parse_ookla_list (self, empty));
  g_assert_cmpuint (velocity_speedtest_get_n_servers (self), ==, 0);
}

static void
test_parse_librespeed_list (void)
{
  g_autoptr (VelocitySpeedtest) self = velocity_speedtest_new ();
  g_autoptr (GBytes) json = bytes_from (
      "[{\"name\":\"Singapore\",\"server\":\"//sg.example.com/\","
      "  \"dlURL\":\"backend/garbage.php\",\"ulURL\":\"backend/empty.php\","
      "  \"pingURL\":\"backend/empty.php\"},"
      " {\"name\":\"Insecure\",\"server\":\"http://plain.example.com/\"}]");

  g_assert_null (parse_librespeed_list (self, json));
  g_assert_cmpuint (velocity_speedtest_get_n_servers (self), ==, 1);
  g_assert_cmpstr (velocity_speedtest_get_server_base_at (self, 0), ==, "https://sg.example.com/");
}

static void
test_find_by_base (void)
{
  g_autoptr (VelocitySpeedtest) self = velocity_speedtest_new ();
  g_autoptr (GBytes) json = bytes_from (
      "[{\"host\":\"one.example.com:8080\",\"name\":\"Manila\",\"sponsor\":\"PLDT\"}]");

  g_assert_null (parse_ookla_list (self, json));

  g_assert_nonnull (find_by_base (self, "https://one.example.com:8080/speedtest/"));
  g_assert_null (find_by_base (self, "https://other.example.com/"));
  g_assert_null (find_by_base (self, NULL));
}

typedef struct
{
  GMainLoop *loop;
  GArray *phases;
  gboolean saw_ping;
} Flow;

static void
on_phase (VelocitySpeedtest *t, int phase, Flow *f)
{
  g_array_append_val (f->phases, phase);

  if (phase == VELOCITY_PHASE_DOWNLOAD || phase == VELOCITY_PHASE_FAILED)
    g_main_loop_quit (f->loop);
}

static void
on_progress (VelocitySpeedtest *t, int phase, double value, Flow *f)
{
  if (phase == VELOCITY_PHASE_PING && value > 0)
    f->saw_ping = TRUE;
}

static void
server_cb (SoupServer *server, SoupServerMessage *msg, const char *path, GHashTable *query, gpointer user_data)
{
  soup_server_message_set_status (msg, SOUP_STATUS_OK, NULL);
  soup_server_message_set_response (msg, "text/plain", SOUP_MEMORY_COPY, "0", 1);
}

static gboolean
timeout_cb (gpointer data)
{
  g_main_loop_quit (data);
  return G_SOURCE_REMOVE;
}

static void
test_ping_then_download (void)
{
  g_autoptr (SoupServer) server = soup_server_new (NULL, NULL);
  g_autoptr (GError) error = NULL;
  g_autoptr (VelocitySpeedtest) t = velocity_speedtest_new ();
  g_autofree char *url = NULL;
  g_autoptr (GSList) uris = NULL;
  Flow f = { 0 };

  soup_server_add_handler (server, NULL, server_cb, NULL, NULL);
  soup_server_listen_local (server, 0, 0, &error);
  g_assert_no_error (error);

  uris = soup_server_get_uris (server);
  url = g_strdup_printf ("http://127.0.0.1:%u/speedtest/", g_uri_get_port (uris->data));
  g_slist_free_full (g_steal_pointer (&uris), (GDestroyNotify) g_uri_unref);

  g_setenv ("VELOCITY_SERVER_URL", url, TRUE);
  g_setenv ("VELOCITY_SERVER_TYPE", "ookla", TRUE);

  f.loop = g_main_loop_new (NULL, FALSE);
  f.phases = g_array_new (FALSE, FALSE, sizeof (int));
  g_signal_connect (t, "phase-changed", G_CALLBACK (on_phase), &f);
  g_signal_connect (t, "progress", G_CALLBACK (on_progress), &f);

  g_timeout_add_seconds (15, timeout_cb, f.loop);
  velocity_speedtest_start (t);
  g_main_loop_run (f.loop);

  velocity_speedtest_cancel (t);

  g_assert_cmpuint (f.phases->len, >=, 2);
  g_assert_cmpint (g_array_index (f.phases, int, 0), ==, VELOCITY_PHASE_PING);
  g_assert_cmpint (g_array_index (f.phases, int, 1), ==, VELOCITY_PHASE_DOWNLOAD);
  g_assert_true (f.saw_ping);
  g_assert_cmpfloat (velocity_speedtest_get_ping (t), >, 0.0);

  g_unsetenv ("VELOCITY_SERVER_URL");
  g_unsetenv ("VELOCITY_SERVER_TYPE");
  g_array_free (f.phases, TRUE);
  g_main_loop_unref (f.loop);
}

static void
test_unreachable_server_fails (void)
{
  g_autoptr (VelocitySpeedtest) t = velocity_speedtest_new ();
  Flow f = { 0 };

  g_setenv ("VELOCITY_SERVER_URL", "http://127.0.0.1:1/speedtest/", TRUE);
  g_setenv ("VELOCITY_SERVER_TYPE", "ookla", TRUE);

  f.loop = g_main_loop_new (NULL, FALSE);
  f.phases = g_array_new (FALSE, FALSE, sizeof (int));
  g_signal_connect (t, "phase-changed", G_CALLBACK (on_phase), &f);

  g_timeout_add_seconds (15, timeout_cb, f.loop);
  velocity_speedtest_start (t);
  g_main_loop_run (f.loop);

  g_assert_cmpint (velocity_speedtest_get_phase (t), ==, VELOCITY_PHASE_FAILED);

  g_unsetenv ("VELOCITY_SERVER_URL");
  g_unsetenv ("VELOCITY_SERVER_TYPE");
  g_array_free (f.phases, TRUE);
  g_main_loop_unref (f.loop);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/servers/normalize-base", test_normalize_base);
  g_test_add_func ("/servers/valid-ookla-host", test_valid_ookla_host);
  g_test_add_func ("/servers/parse-ookla", test_parse_ookla_list);
  g_test_add_func ("/servers/parse-ookla-garbage", test_parse_ookla_rejects_garbage);
  g_test_add_func ("/servers/parse-librespeed", test_parse_librespeed_list);
  g_test_add_func ("/servers/find-by-base", test_find_by_base);
  g_test_add_func ("/engine/ping-then-download", test_ping_then_download);
  g_test_add_func ("/engine/unreachable-server-fails", test_unreachable_server_fails);

  return g_test_run ();
}
