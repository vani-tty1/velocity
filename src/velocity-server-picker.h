#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

typedef void (*VelocityServerPicked) (guint index, gpointer user_data);

void velocity_show_server_dialog (GtkWindow *parent,
                                  GListModel *names,
                                  guint selected,
                                  VelocityServerPicked picked,
                                  gpointer user_data);

G_END_DECLS
