/* velocity-speedtest-private.h
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

#include <libsoup/soup.h>

#include "velocity-speedtest.h"

G_BEGIN_DECLS

/* Internal header: shared by the velocity-speedtest-*.c files only. */

typedef struct
{
  char *name;
  char *base;
  char *dl_path;
  char *ul_path;
  char *ping_path;
  double rtt;
} Candidate;

struct _VelocitySpeedtest
{
  GObject parent_instance;

  SoupSession *session;
  GCancellable *run_cancellable;
  GCancellable *phase_cancellable;
  guint generation;

  VelocityPhase phase;

  // selected server
  char *server_name;
  char *base;
  char *dl_path;
  char *ul_path;
  char *ping_path;

  double ping;
  double jitter;
  double download;
  double upload;

  GPtrArray *candidates;
  int probes_pending;

  GArray *rtts;
  int ping_sent;

  gint64 bytes;
  gint64 t_start;
  gint64 t_last;
  gint64 bytes_last;
  gint64 t_warm;
  gint64 bytes_warm;
  gboolean warm_set;
  double smoothed;
  guint tick_id;
  int active_streams;
  GBytes *upload_blob;
};

/* velocity-speedtest.c: core state machine helpers */
void velocity_speedtest_set_phase (VelocitySpeedtest *self,
                                   VelocityPhase phase);
void velocity_speedtest_fail (VelocitySpeedtest *self,
                              const char *message);
void velocity_speedtest_stop_tick (VelocitySpeedtest *self);
void velocity_speedtest_cancel_everything (VelocitySpeedtest *self);
char *velocity_speedtest_build_url (VelocitySpeedtest *self,
                                    const char *path,
                                    const char *extra_query);
void velocity_speedtest_emit_progress (VelocitySpeedtest *self,
                                       VelocityPhase phase,
                                       double value);
void velocity_speedtest_emit_finished (VelocitySpeedtest *self);

// server list, selection and latency probing
void velocity_speedtest_select_server (VelocitySpeedtest *self);
void velocity_speedtest_candidate_free (Candidate *c);

// latency and jitter measurement
void velocity_speedtest_start_ping (VelocitySpeedtest *self);

// download/upload streams
void velocity_speedtest_start_transfer (VelocitySpeedtest *self,
                                        VelocityPhase phase);

G_END_DECLS

