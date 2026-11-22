/* velocity-server-dialog.c
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

#include "velocity-server-picker.h"
#include <glib/gi18n.h>

typedef struct
{
  GtkSearchEntry *search;
  VelocityServerPicked picked;
  gpointer user_data;
} ServerDialogState;

static gboolean
server_dialog_filter_row (GtkListBoxRow *row,
                          gpointer user_data)
{
  ServerDialogState *state = user_data;
  const char *text = gtk_editable_get_text (GTK_EDITABLE (state->search));
  const char *title = adw_preferences_row_get_title (ADW_PREFERENCES_ROW (row));

  if (text[0] == '\0')
    return TRUE;

  return g_str_match_string (text, title, TRUE);
}

static void
server_dialog_on_search_changed (GtkListBox *list)
{
  gtk_list_box_invalidate_filter (list);
}

static void
server_dialog_on_row_activated (GtkListBox *list,
                                GtkListBoxRow *row,
                                AdwDialog *dialog)
{
  ServerDialogState *state = g_object_get_data (G_OBJECT (dialog), "velocity-server-state");

  state->picked (gtk_list_box_row_get_index (row), state->user_data);
  adw_dialog_close (dialog);
}

void
velocity_show_server_dialog (GtkWindow *parent,
                             GListModel *names,
                             guint selected,
                             VelocityServerPicked picked,
                             gpointer user_data)
{
  ServerDialogState *state = g_new0 (ServerDialogState, 1);
  AdwDialog *dialog = adw_dialog_new ();
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *scroller = gtk_scrolled_window_new ();
  GtkWidget *list = gtk_list_box_new ();
  GtkWidget *search = gtk_search_entry_new ();
  GtkWidget *search_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  state->picked = picked;
  state->user_data = user_data;
  state->search = GTK_SEARCH_ENTRY (search);

  for (guint i = 0; i < g_list_model_get_n_items (names); i++)
    {
      g_autoptr (GtkStringObject) item = g_list_model_get_item (names, i);
      GtkWidget *row = adw_action_row_new ();

      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), gtk_string_object_get_string (item));
      adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
      adw_action_row_set_title_lines (ADW_ACTION_ROW (row), 1);
      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);

      if (i == selected)
        adw_action_row_add_suffix (ADW_ACTION_ROW (row),
                                   gtk_image_new_from_icon_name ("object-select-symbolic"));

      gtk_list_box_append (GTK_LIST_BOX (list), row);
    }

  gtk_widget_add_css_class (list, "boxed-list");
  gtk_widget_set_margin_top (list, 12);
  gtk_widget_set_margin_bottom (list, 12);
  gtk_widget_set_margin_start (list, 12);
  gtk_widget_set_margin_end (list, 12);
  gtk_widget_set_valign (list, GTK_ALIGN_START);
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
  gtk_list_box_set_filter_func (GTK_LIST_BOX (list), server_dialog_filter_row, state, NULL);

  gtk_search_entry_set_placeholder_text (GTK_SEARCH_ENTRY (search), _ ("Search servers"));
  gtk_widget_set_margin_start (search, 12);
  gtk_widget_set_margin_end (search, 12);
  gtk_widget_set_margin_bottom (search, 6);
  gtk_box_append (GTK_BOX (search_box), search);

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), list);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand (scroller, TRUE);

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), adw_header_bar_new ());
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), search_box);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), scroller);

  adw_dialog_set_title (dialog, _ ("Select Server"));
  adw_dialog_set_content_width (dialog, 400);
  adw_dialog_set_content_height (dialog, 650);
  adw_dialog_set_child (dialog, toolbar);

  gtk_search_entry_set_key_capture_widget (GTK_SEARCH_ENTRY (search), GTK_WIDGET (dialog));
  g_signal_connect_swapped (search, "search-changed",
                            G_CALLBACK (server_dialog_on_search_changed), list);
  g_signal_connect (list, "row-activated",
                    G_CALLBACK (server_dialog_on_row_activated), dialog);
  g_object_set_data_full (G_OBJECT (dialog), "velocity-server-state", state, g_free);

  adw_dialog_present (dialog, GTK_WIDGET (parent));
}
