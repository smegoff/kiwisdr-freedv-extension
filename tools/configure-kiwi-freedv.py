#!/usr/bin/env python3
"""Install FreeDV camper settings without placing the shared secret in Kiwi JSON."""

import argparse
import ipaddress
import json
import os
import shutil
import stat
import tempfile
from datetime import datetime, timezone


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("decoder_ip", help="private IPv4 address of the decoder guest")
    parser.add_argument("--config", default="/root/kiwi.config/kiwi.json")
    args = parser.parse_args()
    config_path = args.config
    decoder_ip = str(ipaddress.IPv4Address(args.decoder_ip))
    info = os.stat(config_path)
    with open(config_path, encoding="utf-8") as source:
        config = json.load(source)
    freedv = config.setdefault("freedv", {})
    freedv.pop("decoder_host", None)
    freedv.pop("shared_secret", None)
    freedv.update({
        "decoder_ip": decoder_ip,
        "reporter_enabled": False,
    })
    freedv.setdefault("reporter_callsign", "")
    freedv.setdefault("reporter_grid", "")
    freedv.setdefault("reporter_message", "")

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup = f"{config_path}.pre-freedv-{stamp}"
    shutil.copy2(config_path, backup)
    directory = os.path.dirname(config_path)
    fd, temporary = tempfile.mkstemp(prefix=".kiwi-freedv-", dir=directory, text=True)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            json.dump(config, output, separators=(",", ":"))
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, stat.S_IMODE(info.st_mode) & 0o600)
        os.chown(temporary, info.st_uid, info.st_gid)
        os.replace(temporary, config_path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    print(f"configured decoder IP; root-only secret unchanged; Reporter remains disabled; backup={backup}")


if __name__ == "__main__":
    main()
