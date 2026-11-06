#pragma once

#include <gdk/gdk.h>
#include <glib.h>

G_BEGIN_DECLS

void velocity_accent_init (void);
gboolean velocity_accent_get_rgba (const char *id, GdkRGBA *rgba);
G_END_DECLS

