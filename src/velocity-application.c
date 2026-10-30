/* velocity-application.c
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
#include <glib/gi18n.h>
#include "velocity-application.h"
#include "velocity-window.h"

struct _VelocityApplication
{
	AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE (VelocityApplication, velocity_application, ADW_TYPE_APPLICATION)

VelocityApplication *
velocity_application_new (const char        *application_id,
                          GApplicationFlags  flags)
{
	g_return_val_if_fail (application_id != NULL, NULL);

	return g_object_new (VELOCITY_TYPE_APPLICATION,
	                     "application-id", application_id,
	                     "flags", flags,
	                     "resource-base-path", "/io/github/vani_tty1/velocity",
	                     NULL);
}

static void
velocity_application_activate (GApplication *app)
{
	GtkWindow *window;

	g_assert (VELOCITY_IS_APPLICATION (app));

	window = gtk_application_get_active_window (GTK_APPLICATION (app));

	if (window == NULL)
		window = g_object_new (VELOCITY_TYPE_WINDOW,
		                       "application", app,
		                       NULL);

	gtk_window_present (window);
}

static void
velocity_application_class_init (VelocityApplicationClass *klass)
{
	GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

	app_class->activate = velocity_application_activate;
}

static void
velocity_application_about_action (GSimpleAction *action,
                                   GVariant      *parameter,
                                   gpointer       user_data)
{
    VelocityApplication *self = user_data;
    GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (self));
    g_autofree char *debug_info = NULL;
    g_autofree char *os_release_content = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *debug_text = NULL;
    AdwDialog *dialog;
    static const char *developers[] = {"Giovanni Rafanan <giovannirafanan609@gmail.com>", NULL};
    static const char *designers[] = {"Giovanni Rafanan <giovannirafanan609@gmail.com>", NULL};


    if (!g_file_get_contents("/etc/os-release", &os_release_content, NULL, &error)) {
        os_release_content = g_strdup("Could not read /etc/os-release");
        g_clear_error(&error);
    }

    debug_text = g_strdup_printf (
        "Velocity %s\n"
        "GTK version: %d.%d.%d\n"
        "==> Runtime Info:\n"
        "%s",
        PACKAGE_VERSION,
        gtk_get_major_version (),
        gtk_get_minor_version (),
        gtk_get_micro_version (),
        os_release_content
    );

    dialog = adw_about_dialog_new_from_appdata (
        "/io/github/vani_tty1/velocity/io.github.vani_tty1.velocity.metainfo.xml",
        PACKAGE_VERSION
    );
    adw_about_dialog_set_application_icon (ADW_ABOUT_DIALOG (dialog), "io.github.vani_tty1.velocity");
    adw_about_dialog_set_version (ADW_ABOUT_DIALOG (dialog), PACKAGE_VERSION);
    adw_about_dialog_set_developers (ADW_ABOUT_DIALOG (dialog), developers);
    adw_about_dialog_set_designers (ADW_ABOUT_DIALOG (dialog), designers);
    adw_about_dialog_set_debug_info (ADW_ABOUT_DIALOG (dialog), debug_text);
    adw_about_dialog_set_debug_info_filename (ADW_ABOUT_DIALOG (dialog), "velocity-debug.txt");

    adw_dialog_present (dialog, GTK_WIDGET (window));
}

static void
velocity_application_quit_action (GSimpleAction *action,
                                  GVariant      *parameter,
                                  gpointer       user_data)
{
	VelocityApplication *self = user_data;

	g_assert (VELOCITY_IS_APPLICATION (self));

	g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
	{ "quit", velocity_application_quit_action },
	{ "about", velocity_application_about_action },
};

static void
velocity_application_init (VelocityApplication *self)
{
	g_action_map_add_action_entries (G_ACTION_MAP (self),
	                                 app_actions,
	                                 G_N_ELEMENTS (app_actions),
	                                 self);
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.quit",
	                                       (const char *[]) { "<control>q", NULL });
}
