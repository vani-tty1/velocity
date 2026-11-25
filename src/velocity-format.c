/* velocity-format.c
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

#include "velocity-format.h"
#include <glib/gi18n.h>

char *
velocity_format_mbps (double mbps)
{
  if (mbps >= 1000.0)
    return g_strdup_printf (_ ("%.2f Gbit/s"), mbps / 1000.0);
  if (mbps >= 100.0)
    return g_strdup_printf (_ ("%.0f Mbit/s"), mbps);

  return g_strdup_printf (_ ("%.1f Mbit/s"), mbps);
}

char *
velocity_format_ms (double ms,
                    gboolean with_unit)
{
  if (ms < 10.0)
    return g_strdup_printf (with_unit ? _ ("%.1f ms") : "%.1f", ms);

  return g_strdup_printf (with_unit ? _ ("%.0f ms") : "%.0f", ms);
}
