#pragma once

struct conn_st;

// Called only by the authenticated Kiwi monitor stream command handler.
bool freedv_monitor_poll(struct conn_st *conn_mon, const char *arguments);
void freedv_return_audio(int rx_chan, bool enabled);
