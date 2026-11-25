/* velocity-history.c
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

#include "velocity-details-dialog.h"
#include "velocity-format.h"
#include "velocity-history.h"
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#define HISTORY_MAX 50

struct _VelocityHistory
{
  GtkWindow *window;
  GtkListBox *list;
  GtkStack *stack;
  JsonArray *entries;
};

static char *
history_path (void)
{
  return g_build_filename (g_get_user_data_dir (), "velocity", "history.json", NULL);
}

static GtkWidget *
row_new (JsonObject *entry)
{
  double download = json_object_get_double_member_with_default (entry, "download", 0.0);
  double upload = json_object_get_double_member_with_default (entry, "upload", 0.0);
  g_autofree char *d = velocity_format_mbps (download);
  g_autofree char *u = velocity_format_mbps (upload);
  g_autofree char *title = g_strdup_printf ("↓ %s   ↑ %s", d, u);
  GtkWidget *row = adw_action_row_new ();

  adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);

  // clicking a row opens the full details, see velocity_history_show_details
  adw_action_row_add_suffix (ADW_ACTION_ROW (row),
                             gtk_image_new_from_icon_name ("go-next-symbolic"));
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  g_object_set_data_full (G_OBJECT (row), "entry",
                          json_object_ref (entry),
                          (GDestroyNotify) json_object_unref);

  return row;
}

static char *
history_serialize (VelocityHistory *self,
                   gsize *length)
{
  g_autoptr (JsonGenerator) generator = json_generator_new ();
  g_autoptr (JsonNode) root = json_node_new (JSON_NODE_ARRAY);

  json_node_set_array (root, self->entries);
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);

  return json_generator_to_data (generator, length);
}

static void
history_write (VelocityHistory *self)
{
  g_autofree char *path = history_path ();
  g_autofree char *dir = g_path_get_dirname (path);
  g_autofree char *data = NULL;
  g_autoptr (GError) error = NULL;
  gsize length;

  data = history_serialize (self, &length);
  g_mkdir_with_parents (dir, 0700);
  if (!g_file_set_contents (path, data, length, &error))
    g_warning ("Could not save history: %s", error->message);
}

static void
history_show_empty_if_needed (VelocityHistory *self)
{
  if (json_array_get_length (self->entries) == 0)
    gtk_stack_set_visible_child_name (self->stack, "empty");
}

static GtkListBoxRow *
history_find_row (VelocityHistory *self,
                  JsonObject *entry)
{
  GtkListBoxRow *row;

  for (int i = 0; (row = gtk_list_box_get_row_at_index (self->list, i)) != NULL; i++)
    {
      if (g_object_get_data (G_OBJECT (row), "entry") == entry)
        return row;
    }

  return NULL;
}

static void
history_delete_entry (VelocityHistory *self,
                      JsonObject *entry)
{
  GtkListBoxRow *row = history_find_row (self, entry);

  for (guint i = 0; i < json_array_get_length (self->entries); i++)
    {
      JsonNode *node = json_array_get_element (self->entries, i);

      if (JSON_NODE_HOLDS_OBJECT (node) && json_node_get_object (node) == entry)
        {
          json_array_remove_element (self->entries, i);
          break;
        }
    }

  if (row != NULL)
    gtk_list_box_remove (self->list, GTK_WIDGET (row));

  history_write (self);
  history_show_empty_if_needed (self);
}

static void
on_details_deleted (JsonObject *entry,
                    gpointer user_data)
{
  history_delete_entry (user_data, entry);
}

static void
on_clear_response (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
  VelocityHistory *self = user_data;
  const char *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (source), result);

  if (g_strcmp0 (response, "clear") != 0)
    return;

  gtk_list_box_remove_all (self->list);
  g_clear_pointer (&self->entries, json_array_unref);
  self->entries = json_array_new ();

  history_write (self);
  history_show_empty_if_needed (self);
}

static void
on_export_finished (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
  VelocityHistory *self = user_data;
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
      adw_dialog_present (dialog, GTK_WIDGET (self->window));
    }
}

VelocityHistory *
velocity_history_new (GtkWindow *window,
                      GtkListBox *list,
                      GtkStack *stack)
{
  VelocityHistory *self = g_new0 (VelocityHistory, 1);

  self->window = window;
  self->list = list;
  self->stack = stack;
  self->entries = json_array_new ();

  return self;
}

void
velocity_history_free (VelocityHistory *self)
{
  if (self == NULL)
    return;

  g_clear_pointer (&self->entries, json_array_unref);
  g_free (self);
}

void
velocity_history_load (VelocityHistory *self)
{
  g_autofree char *path = history_path ();
  g_autoptr (JsonParser) parser = json_parser_new ();
  JsonNode *root;

  if (!json_parser_load_from_file (parser, path, NULL))
    return;

  root = json_parser_get_root (parser);
  if (root == NULL || !JSON_NODE_HOLDS_ARRAY (root))
    return;

  g_clear_pointer (&self->entries, json_array_unref);
  self->entries = json_array_ref (json_node_get_array (root));

  // drop the oldest entries if the file grew past the limit
  while (json_array_get_length (self->entries) > HISTORY_MAX)
    json_array_remove_element (self->entries, 0);

  for (guint i = 0; i < json_array_get_length (self->entries); i++)
    {
      JsonNode *node = json_array_get_element (self->entries, i);

      if (JSON_NODE_HOLDS_OBJECT (node))
        gtk_list_box_prepend (self->list,
                              row_new (json_node_get_object (node)));
    }

  if (json_array_get_length (self->entries) > 0)
    gtk_stack_set_visible_child_name (self->stack, "list");
}

void
velocity_history_add_result (VelocityHistory *self,
                             VelocitySpeedtest *test)
{
  const char *server = velocity_speedtest_get_server (test);
  JsonObject *entry = json_object_new ();
  GtkListBoxRow *old;

  json_object_set_int_member (entry, "timestamp", g_get_real_time () / G_USEC_PER_SEC);
  json_object_set_string_member (entry, "server", server != NULL ? server : "");
  json_object_set_double_member (entry, "download", velocity_speedtest_get_download (test));
  json_object_set_double_member (entry, "upload", velocity_speedtest_get_upload (test));
  json_object_set_double_member (entry, "ping", velocity_speedtest_get_ping (test));
  json_object_set_double_member (entry, "jitter", velocity_speedtest_get_jitter (test));
  json_object_set_int_member (entry, "bytes", velocity_speedtest_get_data_used (test));
  json_array_add_object_element (self->entries, entry);

  gtk_list_box_prepend (self->list, row_new (entry));
  gtk_stack_set_visible_child_name (self->stack, "list");

  if (json_array_get_length (self->entries) > HISTORY_MAX)
    {
      json_array_remove_element (self->entries, 0);

      old = gtk_list_box_get_row_at_index (self->list, HISTORY_MAX);
      if (old != NULL)
        gtk_list_box_remove (self->list, GTK_WIDGET (old));
    }

  history_write (self);
}

void
velocity_history_show_details (VelocityHistory *self,
                               GtkListBoxRow *row)
{
  JsonObject *entry = g_object_get_data (G_OBJECT (row), "entry");

  if (entry == NULL)
    return;

  velocity_show_details_dialog (self->window, entry, on_details_deleted, self);
}

void
velocity_history_confirm_clear (VelocityHistory *self)
{
  AdwDialog *dialog = adw_alert_dialog_new (_ ("Clear Results?"),
                                            _ ("All saved results will be deleted."));

  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel", _ ("_Cancel"),
                                  "clear", _ ("_Clear"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "clear",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), GTK_WIDGET (self->window), NULL,
                           on_clear_response, self);
}

void
velocity_history_export (VelocityHistory *self)
{
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();

  gtk_file_dialog_set_initial_name (dialog, "speedtest-results.json");
  gtk_file_dialog_save (dialog, self->window, NULL, on_export_finished, self);
}
