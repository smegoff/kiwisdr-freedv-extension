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
CLIENT_VERSION = "KiwiSDR-FreeDV/0.1.28"
MODE_ACTIVITY_INTERVAL_SECONDS = 10.0
SESSION_TIMEOUT_SECONDS = 15.0


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


def rx_mode_activity(mode, snr=0.0):
    """Advertise the selected receive codec without inventing a callsign."""
    return {"callsign": "", "snr": float(snr), "mode": str(mode or "")}


async def publish_rx_selection(sio, frequency, mode, frequency_changed, mode_changed):
    """Publish frequency before RX mode so the server's frequency reset cannot win a race."""
    if frequency_changed:
        await sio.call("freq_change", {"freq": frequency}, timeout=5)
    if frequency_changed or mode_changed:
        await sio.emit("rx_report", rx_mode_activity(mode))


def mode_activity_due(last_activity, now):
    """Refresh RX mode periodically because Reporter does not replay last-RX state to new viewers."""
    return last_activity is None or now - last_activity >= MODE_ACTIVITY_INTERVAL_SECONDS


class ReporterState:
    def __init__(self, config=None):
        self.sessions = {}
        self.last_reports = {}
        self.revision = 0
        self.last_event_at = None
        self.config = config or {"enabled": False, "callsign": "", "grid_square": "", "message": ""}

    def update(self, event, now=None):
        self.last_event_at = time.monotonic() if now is None else now
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
            # A monotonic clock may return the same value for back-to-back UDP
            # updates. A local sequence makes "most recently synced" exact.
            self.revision += 1
            event["updated"] = self.revision
            self.sessions[sid] = event

    def expire_stale(self, now=None, timeout=SESSION_TIMEOUT_SECONDS):
        """Clear presence if decoder events stop before a stop datagram arrives."""
        current = time.monotonic() if now is None else now
        if (self.sessions and self.last_event_at is not None and
                current - self.last_event_at > timeout):
            self.sessions.clear()
            return True
        return False

    def selected(self):
        synced = [s for s in self.sessions.values() if s.get("sync")]
        if synced:
            return max(synced, key=lambda x: x["updated"])
        # Publish the tuned frequency as soon as an RX-only session starts.
        # A synchronized session still takes precedence when one exists.
        return max(self.sessions.values(), key=lambda x: x["updated"], default=None)

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


async def connect_and_wait_for_acceptance(sio, url, auth, timeout=10):
    """Connect and wait for the Reporter's application-level acceptance.

    The server uses Socket.IO ``always_connect`` mode, so the generic connect
    callback can run before it has validated the station identity. Only the
    explicit ``connection_successful`` event means the station is listed.
    """
    loop = asyncio.get_running_loop()
    accepted = loop.create_future()

    @sio.on("connection_successful")
    async def connection_successful(_data=None):
        if not accepted.done():
            accepted.set_result(True)

    @sio.event
    async def connect_error(data):
        if not accepted.done():
            accepted.set_exception(ConnectionError(f"Reporter rejected connection: {data}"))

    @sio.event
    async def disconnect(reason=None):
        if not accepted.done():
            accepted.set_exception(ConnectionError(f"Reporter disconnected before acceptance: {reason}"))

    try:
        await sio.connect(url, auth=auth, wait_timeout=timeout)
        await asyncio.wait_for(asyncio.shield(accepted), timeout=timeout)
    except Exception:
        if not accepted.done():
            accepted.cancel()
        if sio.connected:
            await sio.disconnect()
        raise


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
    last_mode_activity = None
    write_state("disabled")

    async def disconnect(new_state="disabled"):
        nonlocal last_freq, last_mode, last_message, last_mode_activity
        if sio.connected:
            await sio.disconnect()
        last_freq = last_mode = last_message = None
        last_mode_activity = None
        write_state(new_state)

    while True:
        try:
            event = await asyncio.wait_for(queue.get(), timeout=1.0)
            state.update(event)
        except asyncio.TimeoutError:
            event = None

        if state.expire_stale():
            logging.info("expired stale Reporter session state")

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
                await connect_and_wait_for_acceptance(sio, url, build_auth(cfg), timeout=10)
                retry_delay, next_retry = 1.0, 0.0
                last_freq = last_mode = last_message = None
                last_mode_activity = None
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
                now = time.monotonic()
                frequency_changed = frequency != last_freq
                mode_changed = mode != last_mode
                activity_due = mode_activity_due(last_mode_activity, now)
                await publish_rx_selection(sio, frequency, mode, frequency_changed, mode_changed or activity_due)
                if frequency_changed or mode_changed or activity_due:
                    last_mode_activity = now
                if frequency_changed:
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
                    last_mode_activity = time.monotonic()
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
