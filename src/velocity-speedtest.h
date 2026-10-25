/* velocity-speedtest.h
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

#include <glib-object.h>

G_BEGIN_DECLS

#define VELOCITY_TYPE_SPEEDTEST (velocity_speedtest_get_type ())

G_DECLARE_FINAL_TYPE (VelocitySpeedtest, velocity_speedtest, VELOCITY, SPEEDTEST, GObject)

typedef enum
{
  VELOCITY_PHASE_IDLE,
  VELOCITY_PHASE_SERVER,
  VELOCITY_PHASE_PING,
  VELOCITY_PHASE_DOWNLOAD,
  VELOCITY_PHASE_UPLOAD,
  VELOCITY_PHASE_DONE,
  VELOCITY_PHASE_FAILED,
} VelocityPhase;

VelocitySpeedtest *velocity_speedtest_new (void);

void velocity_speedtest_start (VelocitySpeedtest *self);
void velocity_speedtest_cancel (VelocitySpeedtest *self);

VelocityPhase velocity_speedtest_get_phase (VelocitySpeedtest *self);
const char *velocity_speedtest_get_server (VelocitySpeedtest *self);
double velocity_speedtest_get_ping (VelocitySpeedtest *self);
double velocity_speedtest_get_jitter (VelocitySpeedtest *self);
double velocity_speedtest_get_download (VelocitySpeedtest *self);
double velocity_speedtest_get_upload (VelocitySpeedtest *self);

G_END_DECLS

