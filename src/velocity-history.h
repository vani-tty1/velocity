/* velocity-history.h
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

#pragma once

#include "velocity-speedtest.h"
#include <adwaita.h>

G_BEGIN_DECLS

typedef struct _VelocityHistory VelocityHistory;

VelocityHistory *velocity_history_new (GtkWindow *window,
                                       GtkListBox *list,
                                       GtkStack *stack);
void velocity_history_free (VelocityHistory *self);
void velocity_history_load (VelocityHistory *self);
void velocity_history_add_result (VelocityHistory *self,
                                  VelocitySpeedtest *test);
void velocity_history_show_details (VelocityHistory *self,
                                    GtkListBoxRow *row);
void velocity_history_confirm_clear (VelocityHistory *self);
void velocity_history_export (VelocityHistory *self);

G_END_DECLS
