#!/usr/bin/env python3
"""RX-only FreeDV Reporter bridge.

The decoder daemon sends newline-delimited JSON events to UDP localhost:8075.
No listener identity is accepted or forwarded.
"""

import asyncio
import json
import logging
import os
import random
import re
import signal
import time

CALLSIGN = re.compile(r"^(([A-Za-z0-9]+/)?[A-Za-z0-9]{1,3}[0-9][A-Za-z0-9]*[A-Za-z](/[A-Za-z0-9]+)?)$")
GRID = re.compile(r"^[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2})?$")
CLIENT_VERSION = "KiwiSDR-FreeDV/0.1.13"


def build_auth(config):
    """Return the RX-only authentication payload accepted by FreeDV Reporter."""
    return {
        "role": "report_wo",
        "callsign": config["callsign"],
        "grid_square": config["grid_square"],
        "version": CLIENT_VERSION,
        "rx_only": True,
        "os": "linux",
        "protocol_version": 2,
    }


class ReporterState:
    def __init__(self, config=None):
        self.sessions = {}
        self.last_reports = {}
        self.config = config or {"enabled": False, "callsign": "", "grid_square": "", "message": ""}

    def update(self, event):
        sid = int(event["session_id"])
        if "enabled" in event:
            self.config = {
                "enabled": bool(event.get("enabled")),
                "callsign": str(event.get("station_callsign", "")).upper(),
                "grid_square": str(event.get("grid_square", "")).upper(),
                "message": str(event.get("message", ""))[:128],
            }
        if event["type"] == "stop":
            self.sessions.pop(sid, None)
        else:
            event["updated"] = time.monotonic()
            self.sessions[sid] = event

    def selected(self):
        synced = [s for s in self.sessions.values() if s.get("sync")]
        return max(synced, key=lambda x: x["updated"], default=None)

    def reportable(self, callsign, mode, frequency):
        if not callsign or not CALLSIGN.fullmatch(callsign):
            return False
        key = (callsign.upper(), mode, int(frequency))
        now = time.monotonic()
        if now - self.last_reports.get(key, 0) < 10:
            return False
        self.last_reports[key] = now
        return True


class Datagram(asyncio.DatagramProtocol):
    def __init__(self, queue): self.queue = queue
    def datagram_received(self, data, _addr):
        try:
            event = json.loads(data.decode("utf-8"))
            allowed = {"type", "session_id", "frequency", "mode", "sync", "snr", "callsign",
                       "enabled", "station_callsign", "grid_square", "message"}
            self.queue.put_nowait({k: event[k] for k in allowed if k in event})
        except (ValueError, KeyError, UnicodeDecodeError, asyncio.QueueFull):
            logging.warning("discarded invalid reporter event")


def write_state(value):
    path = os.getenv("FREEDV_REPORTER_STATE_FILE", "/tmp/freedv-reporter-state")
    try:
        with open(path, "w", encoding="ascii") as output:
            output.write(value + "\n")
    except OSError as exc:
        logging.warning("unable to write Reporter state: %s", exc)


async def main():
    import socketio
    initial = {
        "enabled": os.getenv("FREEDV_REPORTER_ENABLED", "0") == "1",
        "callsign": os.getenv("FREEDV_REPORTER_CALLSIGN", "").upper(),
        "grid_square": os.getenv("FREEDV_REPORTER_GRID", "").upper(),
        "message": os.getenv("FREEDV_REPORTER_MESSAGE", "")[:128],
    }
    url = os.getenv("FREEDV_REPORTER_URL", "https://qso.freedv.org")
    queue = asyncio.Queue(maxsize=64)
    loop = asyncio.get_running_loop()
    await loop.create_datagram_endpoint(lambda: Datagram(queue), local_addr=("127.0.0.1", 8075))
    state = ReporterState(initial)
    sio = socketio.AsyncClient(reconnection=False, logger=False)
    retry_delay, next_retry = 1.0, 0.0
    last_freq = last_mode = last_message = None
    last_rade_activity = 0.0
    write_state("disabled")

    async def disconnect(new_state="disabled"):
        nonlocal last_freq, last_mode, last_message
        if sio.connected:
            await sio.disconnect()
        last_freq = last_mode = last_message = None
        write_state(new_state)

    while True:
        try:
            event = await asyncio.wait_for(queue.get(), timeout=1.0)
            state.update(event)
        except asyncio.TimeoutError:
            event = None

        cfg = state.config
        if not state.sessions:
            await disconnect()
            retry_delay, next_retry = 1.0, 0.0
            continue
        if not cfg.get("enabled"):
            await disconnect()
            continue
        if not CALLSIGN.fullmatch(cfg.get("callsign", "")) or not GRID.fullmatch(cfg.get("grid_square", "")):
            await disconnect("error")
            continue

        if not sio.connected and time.monotonic() >= next_retry:
            write_state("connecting")
            try:
                await sio.connect(url, auth=build_auth(cfg), wait_timeout=10)
                retry_delay, next_retry = 1.0, 0.0
                last_freq = last_mode = last_message = None
                write_state("online")
            except Exception as exc:
                label = "rate-limited" if "429" in str(exc) else "error"
                write_state(label)
                logging.warning("Reporter connect failed: %s", exc)
                next_retry = time.monotonic() + retry_delay * random.uniform(0.75, 1.25)
                retry_delay = min(retry_delay * 2, 30.0)
                continue

        if not sio.connected:
            continue
        try:
            selected = state.selected()
            if selected:
                frequency = int(selected.get("frequency", 0))
                mode = selected.get("mode", "")
                if last_mode is not None and mode != last_mode:
                    await sio.emit("rx_report", {"callsign": "", "snr": 0, "mode": ""})
                if frequency != last_freq:
                    await sio.emit("freq_change", {"freq": frequency})
                    last_freq = frequency
                last_mode = mode
            if cfg.get("message", "") != last_message:
                await sio.emit("message_update", {"message": cfg.get("message", "")})
                last_message = cfg.get("message", "")
            if event:
                call = str(event.get("callsign", "")).upper()
                mode = event.get("mode", "")
                freq = int(event.get("frequency", 0))
                if state.reportable(call, mode, freq):
                    await sio.emit("rx_report", {"callsign": call, "snr": float(event.get("snr", 0)), "mode": mode})
                elif mode == "RADEV1" and event.get("sync") and time.monotonic() - last_rade_activity >= 1:
                    await sio.emit("rx_report", {"callsign": "", "snr": float(event.get("snr", 0)), "mode": mode})
                    last_rade_activity = time.monotonic()
        except Exception as exc:
            logging.warning("Reporter update failed: %s", exc)
            await disconnect("error")
            next_retry = time.monotonic() + retry_delay * random.uniform(0.75, 1.25)
            retry_delay = min(retry_delay * 2, 30.0)


if __name__ == "__main__":
    logging.basicConfig(level=os.getenv("LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
    asyncio.run(main())
