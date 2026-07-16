#pragma once

struct conn_st;

// Called only by the authenticated Kiwi monitor stream command handler.
bool freedv_monitor_poll(struct conn_st *conn_mon, const char *arguments);
// Relay decoder metadata directly from the authenticated camper. This avoids
// depending on a receiver connection callback that can be replaced during the
// monitor-to-camper transition.
bool freedv_receive_cmds(u2_t key, char *cmd, int rx_chan);
void freedv_return_audio(int rx_chan, bool enabled);
