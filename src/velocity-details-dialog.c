/* velocity-details-dialog.c
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
#include <glib/gi18n.h>

typedef struct
{
  AdwDialog *dialog;
  JsonObject *entry;
  VelocityDetailsDeleted deleted;
  gpointer user_data;
} DetailsState;

static void
details_state_free (DetailsState *state)
{
  json_object_unref (state->entry);
  g_free (state);
}

static void
details_add_row (AdwPreferencesGroup *group,
                 const char *title,
                 const char *value)
{
  GtkWidget *row = adw_action_row_new ();

  adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), value);
  gtk_widget_add_css_class (row, "property");
  adw_preferences_group_add (group, row);
}

static void
details_fill_group (AdwPreferencesGroup *group,
                    JsonObject *entry)
{
  g_autoptr (GDateTime) dt = NULL;
  g_autofree char *when = NULL;
  g_autofree char *d = NULL;
  g_autofree char *u = NULL;
  g_autofree char *p = NULL;
  const char *server;

  dt = g_date_time_new_from_unix_local (json_object_get_int_member_with_default (entry, "timestamp", 0));
  when = g_date_time_format (dt, "%x %X");
  server = json_object_get_string_member_with_default (entry, "server", "");
  d = velocity_format_mbps (json_object_get_double_member_with_default (entry, "download", 0.0));
  u = velocity_format_mbps (json_object_get_double_member_with_default (entry, "upload", 0.0));
  p = velocity_format_ms (json_object_get_double_member_with_default (entry, "ping", 0.0), TRUE);

  details_add_row (group, _ ("Date"), when);
  details_add_row (group, _ ("Server"),
                   server[0] != '\0' ? server : _ ("Unknown"));
  details_add_row (group, _ ("Download"), d);
  details_add_row (group, _ ("Upload"), u);
  details_add_row (group, _ ("Ping"), p);

  // older history entries were saved without jitter
  if (json_object_has_member (entry, "jitter"))
    {
      g_autofree char *j = g_strdup_printf (_ ("%.1f ms"),
                                            json_object_get_double_member (entry, "jitter"));

      details_add_row (group, _ ("Jitter"), j);
    }

  // older history entries were saved without data used
  if (json_object_has_member (entry, "bytes"))
    {
      g_autofree char *b = g_format_size (json_object_get_int_member (entry, "bytes"));

      details_add_row (group, _ ("Data used"), b);
    }
}

static void
on_delete_clicked (GtkButton *button,
                   DetailsState *state)
{
  AdwDialog *dialog = state->dialog;
  VelocityDetailsDeleted deleted = state->deleted;
  gpointer user_data = state->user_data;
  JsonObject *entry = json_object_ref (state->entry);

  // closing may free the state, so copy what we need first
  adw_dialog_close (dialog);
  deleted (entry, user_data);
  json_object_unref (entry);
}

void
velocity_show_details_dialog (GtkWindow *parent,
                              JsonObject *entry,
                              VelocityDetailsDeleted deleted,
                              gpointer user_data)
{
  DetailsState *state = g_new0 (DetailsState, 1);
  AdwDialog *dialog = adw_dialog_new ();
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *page = adw_preferences_page_new ();
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *delete_button = gtk_button_new_with_label (_ ("Delete Result"));

  details_fill_group (ADW_PREFERENCES_GROUP (group), entry);
  adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));

  gtk_widget_set_halign (delete_button, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (delete_button, 12);
  gtk_widget_set_margin_bottom (delete_button, 12);
  gtk_widget_add_css_class (delete_button, "pill");
  gtk_widget_add_css_class (delete_button, "destructive-action");

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), adw_header_bar_new ());
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), page);
  adw_toolbar_view_add_bottom_bar (ADW_TOOLBAR_VIEW (toolbar), delete_button);

  state->dialog = dialog;
  state->entry = json_object_ref (entry);
  state->deleted = deleted;
  state->user_data = user_data;
  g_object_set_data_full (G_OBJECT (dialog), "state", state,
                          (GDestroyNotify) details_state_free);
  g_signal_connect (delete_button, "clicked", G_CALLBACK (on_delete_clicked), state);

  adw_dialog_set_title (dialog, _ ("Test Details"));
  adw_dialog_set_content_width (dialog, 400);
  adw_dialog_set_content_height (dialog, 650);
  adw_dialog_set_child (dialog, toolbar);
  adw_dialog_present (dialog, GTK_WIDGET (parent));
}
