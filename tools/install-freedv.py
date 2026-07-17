#!/usr/bin/env python3
"""Safety-gated Kiwi-side installer for local or remote FreeDV decoding."""

from __future__ import annotations

import argparse
import ipaddress
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import sys
import time
from typing import Optional
import urllib.request


RISK_PHRASE = "I ACCEPT THE KIWI DEPLOYMENT RISK"
PINNED_KIWI_COMMIT = "417e2c8add196e879b8cc4eb4a488b35b4bf0df7"
DISCLAIMER = """
RISK NOTICE
This script builds and replaces the executable used by a live KiwiSDR. A power
failure, storage failure, incompatible source tree or interrupted operation can
leave the receiver unavailable. Atomic software rollback cannot repair damaged
eMMC, a bootloader failure or hardware. A supported bootable backup microSD is
the only full physical recovery path. Remote decoder snapshots and archives are
rollback aids, not substitutes for the Kiwi recovery card.
""".strip()


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--mode", choices=("local", "remote"))
    result.add_argument("--decoder-ip", help="private IPv4 address of the remote decoder")
    result.add_argument("--kiwi-ip", help="Kiwi IPv4 address reachable by the remote decoder")
    result.add_argument("--decoder-ssh", help="verified root SSH target, normally root@<decoder-ip>")
    remote = result.add_mutually_exclusive_group()
    remote.add_argument("--install-remote-decoder", action="store_true")
    remote.add_argument("--skip-remote-install", action="store_true")
    modem = result.add_mutually_exclusive_group()
    modem.add_argument("--enable-rade", action="store_true", help="enable experimental RADEV1 after offline tests")
    modem.add_argument("--legacy-only", action="store_true", help="keep RADEV1 disabled (the safe default)")
    recovery = result.add_mutually_exclusive_group()
    recovery.add_argument("--backup-sd-present", action="store_true")
    recovery.add_argument("--accept-reduced-recovery", action="store_true")
    result.add_argument("--backup-copy", help="independent scp destination for local mode")
    result.add_argument("--decoder-rollback-ready", action="store_true")
    result.add_argument("--kiwi-source", default="/root/KiwiSDR")
    result.add_argument("--repo", default=str(Path(__file__).resolve().parents[1]))
    result.add_argument("--release")
    result.add_argument("--build-timeout", type=int, default=3600)
    result.add_argument("--acknowledge-risk", action="store_true")
    result.add_argument("--yes", action="store_true", help="non-interactive after all safety flags are supplied")
    result.add_argument("--dry-run", action="store_true")
    return result


def ask(prompt: str, default: Optional[str] = None) -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"{prompt}{suffix}: ").strip()
    return value or (default or "")


def yes_no(prompt: str, default: bool) -> bool:
    marker = "Y/n" if default else "y/N"
    value = input(f"{prompt} [{marker}]: ").strip().lower()
    if not value:
        return default
    return value in {"y", "yes"}


def validate_scp(value: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_.-]+@[A-Za-z0-9_.-]+:/[^\s]+", value):
        raise ValueError("backup copy must use user@host:/absolute/path")
    return value


def resolve_options(args: argparse.Namespace) -> argparse.Namespace:
    interactive = not args.yes and sys.stdin.isatty()
    if not args.mode:
        if not interactive:
            raise ValueError("--mode is required in non-interactive operation")
        choice = ask("Decoder location: 1=local AI-64, 2=remote VM/LXC", "2")
        args.mode = "local" if choice == "1" else "remote"

    if args.mode == "remote":
        if not args.decoder_ip:
            if not interactive:
                raise ValueError("--decoder-ip is required for remote mode")
            args.decoder_ip = ask("Remote decoder private IPv4 address")
        args.decoder_ip = str(ipaddress.IPv4Address(args.decoder_ip))
        if not args.decoder_ssh:
            args.decoder_ssh = f"root@{args.decoder_ip}"
        if not re.fullmatch(r"root@[A-Za-z0-9_.-]+", args.decoder_ssh):
            raise ValueError("remote installation requires a root@host SSH target")
        if not args.kiwi_ip:
            if interactive:
                args.kiwi_ip = ask("Kiwi IPv4 address reachable from the decoder", auto_kiwi_ip(args.decoder_ip))
            else:
                raise ValueError("--kiwi-ip is required for remote non-interactive operation")
        args.kiwi_ip = str(ipaddress.IPv4Address(args.kiwi_ip))
        if not args.install_remote_decoder and not args.skip_remote_install:
            if not interactive:
                raise ValueError("choose --install-remote-decoder or --skip-remote-install")
            args.install_remote_decoder = yes_no("Install/update decoder software over verified SSH?", True)
            args.skip_remote_install = not args.install_remote_decoder
        if not args.decoder_rollback_ready:
            if interactive:
                args.decoder_rollback_ready = yes_no(
                    "Does the remote decoder have a current snapshot or independent backup?", False
                )
            if not args.decoder_rollback_ready:
                raise ValueError("remote decoder rollback readiness is required")

    if not args.backup_sd_present and not args.accept_reduced_recovery:
        if not interactive:
            raise ValueError("choose --backup-sd-present or --accept-reduced-recovery")
        args.backup_sd_present = yes_no("Do you have a verified bootable Kiwi backup microSD?", False)
        if not args.backup_sd_present:
            phrase = ask("Type REDUCED RECOVERY to continue without physical recovery")
            if phrase != "REDUCED RECOVERY":
                raise ValueError("reduced recovery was not acknowledged")
            args.accept_reduced_recovery = True

    if args.mode == "local" and args.accept_reduced_recovery and not args.backup_copy:
        if not interactive:
            raise ValueError("local reduced-recovery mode requires --backup-copy")
        args.backup_copy = ask("Independent backup destination (user@host:/absolute/path)")
    if args.backup_copy:
        validate_scp(args.backup_copy)

    if interactive and not args.enable_rade and not args.legacy_only:
        args.enable_rade = yes_no("Enable experimental RADEV1 after its offline validation passes?", False)
        args.legacy_only = not args.enable_rade
    if not args.enable_rade:
        args.legacy_only = True

    if not args.acknowledge_risk:
        if not interactive:
            raise ValueError("--acknowledge-risk is required")
        print(DISCLAIMER)
        args.acknowledge_risk = ask(f"Type exactly: {RISK_PHRASE}") == RISK_PHRASE
    if not args.acknowledge_risk:
        raise ValueError("deployment risk was not acknowledged")

    if not args.release:
        version = extension_version(Path(args.repo))
        args.release = f"freedv-v{version.replace('.', '-')}-{time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())}"
    if not re.fullmatch(r"freedv-v[0-9A-Za-z.-]+", args.release):
        raise ValueError("release label contains unsupported characters")
    if args.build_timeout < 300 or args.build_timeout > 7200:
        raise ValueError("--build-timeout must be between 300 and 7200 seconds")
    return args


def extension_version(repo: Path) -> str:
    source = (repo / "kiwi-overlay/extensions/FreeDV/freedv.cpp").read_text(encoding="utf-8")
    match = re.search(r'^#define FREEDV_RELEASE "([0-9.]+)"', source, re.MULTILINE)
    if not match:
        raise ValueError("unable to determine extension release")
    return match.group(1)


def auto_kiwi_ip(decoder_ip: str) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect((decoder_ip, 9))
        return probe.getsockname()[0]


def run(command: list[str], *, capture: bool = False, input_bytes: Optional[bytes] = None) -> str:
    display = " ".join(command)
    print(f"+ {display}")
    completed = subprocess.run(
        command,
        check=True,
        input=input_bytes,
        stdout=subprocess.PIPE if capture else None,
        text=False,
    )
    return completed.stdout.decode().strip() if capture and completed.stdout else ""


def ssh(target: str, command: str, *, capture: bool = False, input_bytes: Optional[bytes] = None) -> str:
    return run(
        ["ssh", "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=10", target, command],
        capture=capture,
        input_bytes=input_bytes,
    )


def status() -> dict[str, str]:
    with urllib.request.urlopen("http://127.0.0.1:8073/status", timeout=5) as response:
        lines = response.read().decode(errors="replace").splitlines()
    return dict(line.split("=", 1) for line in lines if "=" in line)


def zero_listener_gate() -> None:
    for attempt in range(2):
        current = status()
        if current.get("status") != "active" or current.get("users") != "0":
            raise RuntimeError("Kiwi must be healthy with zero listeners")
        if attempt == 0:
            print("Zero listeners confirmed; checking again in ten seconds")
            time.sleep(10)


def ensure_secret(path: Path) -> bytes:
    if path.exists():
        content = path.read_text(encoding="utf-8")
        match = re.search(r"^FREEDV_SHARED_SECRET=([0-9A-Fa-f]{64})$", content, re.MULTILINE)
        if not match:
            raise RuntimeError(f"invalid shared secret in {path}")
        return f"FREEDV_SHARED_SECRET={match.group(1)}\n".encode()
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as secret_file:
        secret_file.write(f"FREEDV_SHARED_SECRET={secrets.token_hex(32)}\n")
    return path.read_bytes()


def local_debian_release() -> str:
    values = {}
    for line in Path("/etc/os-release").read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip().strip('"')
    if values.get("ID") != "debian" or values.get("VERSION_ID") not in {"11", "12"}:
        raise RuntimeError("the Kiwi host must run validated Debian 11 or Debian 12")
    return values["VERSION_ID"]


def preflight(args: argparse.Namespace) -> tuple[str, Optional[str], Optional[str]]:
    if not hasattr(os, "geteuid") or os.geteuid() != 0:
        raise RuntimeError("run the installer as root on the Kiwi")
    for command in ("git", "wget", "tar", "sha256sum", "systemctl", "pgrep"):
        if not shutil.which(command):
            raise RuntimeError(f"required command is missing: {command}")
    if args.mode == "remote":
        for command in ("ssh", "scp"):
            if not shutil.which(command):
                raise RuntimeError(f"required remote-install command is missing: {command}")
    repo = Path(args.repo)
    kiwi = Path(args.kiwi_source)
    if not (repo / "tools/apply-kiwi-overlay.sh").is_file() or not (kiwi / "Makefile").is_file():
        raise RuntimeError("repository or Kiwi source checkout is missing")
    commit = run(["git", "-C", str(kiwi), "rev-parse", "HEAD"], capture=True)
    if commit != PINNED_KIWI_COMMIT:
        raise RuntimeError(f"Kiwi source must be pinned to {PINNED_KIWI_COMMIT}")
    current = status()
    if current.get("status") != "active" or current.get("sw_version") != "KiwiSDR_v1.901":
        raise RuntimeError("this installer is validated only for a healthy KiwiSDR 1.901")
    if shutil.disk_usage(args.kiwi_source).free < 400 * 1024 * 1024:
        raise RuntimeError("at least 400 MiB free Kiwi storage is required")
    wrappers = subprocess.run(
        ["pgrep", "-f", r"(deploy|rollback)-kiwi-release[.]sh"],
        stdout=subprocess.DEVNULL,
        check=False,
    )
    if wrappers.returncode == 0:
        raise RuntimeError("a Kiwi deployment wrapper is already running")
    if args.mode == "remote":
        decoder_details = ssh(
            args.decoder_ssh,
            ". /etc/os-release; test $(id -u) = 0; "
            "test \"$ID\" = debian; case \"$VERSION_ID\" in 11|12) ;; *) exit 2;; esac; "
            "case $(uname -m) in x86_64|aarch64) ;; *) exit 3;; esac; "
            "printf '%s %s %s' \"$VERSION_ID\" \"$(uname -m)\" \"$(date +%s)\"",
            capture=True,
        )
        decoder_release, decoder_arch, decoder_epoch = decoder_details.split()
        if abs(int(decoder_epoch) - int(time.time())) > 20:
            raise RuntimeError("decoder clock differs from the Kiwi by more than 20 seconds")
    else:
        decoder_release = None
        decoder_arch = None
    return local_debian_release(), decoder_release, decoder_arch


def stream_repository(repo: Path, target: str, destination: str) -> None:
    print(f"+ git archive HEAD | ssh {target} tar -x -C {destination}")
    archive = subprocess.Popen(["git", "-C", str(repo), "archive", "--format=tar", "HEAD"], stdout=subprocess.PIPE)
    assert archive.stdout is not None
    remote = subprocess.run(
        [
            "ssh", "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=10", target,
            f"install -d -m 0755 {destination} && tar -x -C {destination}",
        ],
        stdin=archive.stdout,
        check=False,
    )
    archive.stdout.close()
    archive_rc = archive.wait()
    if archive_rc or remote.returncode:
        raise RuntimeError("repository transfer to decoder failed")


def set_env_value(path: Path, key: str, value: str) -> None:
    original = path.read_text(encoding="utf-8").splitlines()
    replaced = False
    output = []
    for line in original:
        if line.startswith(f"{key}="):
            output.append(f"{key}={value}")
            replaced = True
        else:
            output.append(line)
    if not replaced:
        output.append(f"{key}={value}")
    temporary = path.with_name(f".{path.name}.one-shot")
    temporary.write_text("\n".join(output) + "\n", encoding="utf-8")
    shutil.copymode(path, temporary)
    if hasattr(os, "chown"):
        info = path.stat()
        os.chown(temporary, info.st_uid, info.st_gid)
    temporary.replace(path)


def wait_build(timeout: int) -> None:
    exit_file = Path("/root/freedv-build.exit")
    deadline = time.monotonic() + timeout
    last_notice = 0.0
    while not exit_file.exists():
        if time.monotonic() >= deadline:
            raise RuntimeError("Kiwi build timed out; it was not deployed")
        if time.monotonic() - last_notice >= 60:
            print("Kiwi production build is still running")
            last_notice = time.monotonic()
        time.sleep(5)
    if exit_file.read_text(encoding="utf-8").strip() != "0":
        raise RuntimeError("Kiwi production build failed; see /root/freedv-build.log")


def decoder_health(args: argparse.Namespace) -> bool:
    command = (
        "health=$(wget -q -T 5 -O - http://127.0.0.1:8074/healthz) && "
        "case $health in *'\"status\":\"ok\"'*'\"kiwi_connected\":true'*) exit 0;; *) exit 1;; esac"
    )
    if args.mode == "local":
        return subprocess.run(["bash", "-c", command], check=False).returncode == 0
    return subprocess.run(
        ["ssh", "-o", "StrictHostKeyChecking=yes", args.decoder_ssh, command], check=False
    ).returncode == 0


def stop_decoder(args: argparse.Namespace) -> None:
    command = "systemctl stop freedv-decoder.service freedv-reporter.service 2>/dev/null || true"
    if args.mode == "local":
        subprocess.run(["bash", "-c", command], check=False)
    else:
        subprocess.run(["ssh", "-o", "StrictHostKeyChecking=yes", args.decoder_ssh, command], check=False)


def rollback_prepared_decoder(args: argparse.Namespace, remote_source: str) -> None:
    if args.mode == "remote":
        command = f"{remote_source}/deploy/rollback-remote-decoder.sh"
        result = subprocess.run(
            ["ssh", "-o", "StrictHostKeyChecking=yes", args.decoder_ssh, command], check=False
        )
        if result.returncode != 0:
            print("WARNING: automatic decoder restore failed; use the confirmed guest snapshot", file=sys.stderr)
    else:
        result = subprocess.run(
            ["/bin/bash", str(Path(args.repo) / "deploy/rollback-ai64-preparation.sh")], check=False
        )
        if result.returncode != 0:
            print("WARNING: local decoder preparation cleanup failed; Kiwi rollback was still attempted", file=sys.stderr)


def commit_prepared_decoder(args: argparse.Namespace) -> None:
    if args.mode == "remote":
        ssh(args.decoder_ssh, "rm -f /var/lib/freedv-decoder/one-shot-ready")
    else:
        Path("/var/lib/freedv-decoder/one-shot-local-ready").unlink(missing_ok=True)


def wait_kiwi_health(timeout: int = 120) -> None:
    deadline = time.monotonic() + timeout
    last_error: Optional[Exception] = None
    while time.monotonic() < deadline:
        try:
            current = status()
            if current.get("status") == "active" and current.get("sw_version") == "KiwiSDR_v1.901":
                return
        except (OSError, ValueError) as error:
            last_error = error
        time.sleep(2)
    detail = f": {last_error}" if last_error else ""
    raise RuntimeError(f"Kiwi did not recover to a healthy 1.901 state{detail}")


def main() -> int:
    args = resolve_options(parser().parse_args())
    print(DISCLAIMER)
    print("\nINSTALL PLAN")
    print(f"  decoder mode: {args.mode}")
    if args.mode == "remote":
        print(f"  decoder IP: {args.decoder_ip}")
        print(f"  decoder SSH: {args.decoder_ssh}")
        print(f"  install decoder: {args.install_remote_decoder}")
    print(f"  release: {args.release}")
    print(f"  physical SD recovery: {args.backup_sd_present}")
    print(f"  RADEV1: {'enabled after offline gate' if args.enable_rade else 'disabled'}")
    print("  Reporter: disabled until separately opted in")
    if args.dry_run:
        print("DRY RUN: no host, decoder, configuration or service was changed")
        return 0
    if not args.yes and ask("Type APPLY to begin") != "APPLY":
        raise ValueError("installation cancelled")

    kiwi_debian, decoder_debian, decoder_arch = preflight(args)
    print(f"Detected Kiwi operating system: Debian {kiwi_debian}")
    if decoder_debian:
        print(f"Detected remote decoder: Debian {decoder_debian} on {decoder_arch}")
    elif args.mode == "local":
        print(f"Local decoder will target Debian {kiwi_debian}")
    zero_listener_gate()
    repo = Path(args.repo)
    kiwi = Path(args.kiwi_source)
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    secret_path = Path("/root/decoder.env")
    secret_content = ensure_secret(secret_path)
    backup = Path(run([str(repo / "tools/backup-kiwi-on-device.sh"), "/root/freedv-installer-backups", stamp], capture=True).splitlines()[-1])

    previous = "baseline-1.901"
    active_link = Path("/root/freedv-releases/active")
    if active_link.is_symlink():
        previous = os.readlink(active_link)
    config_changed = False
    deployed = False
    decoder_prepared = False
    remote_source = f"/opt/kiwi-freedv-one-shot-{stamp}"
    try:
        if args.mode == "remote":
            remote_backup = f"/var/backups/kiwi-freedv/{stamp}"
            ssh(args.decoder_ssh, f"install -d -m 0700 {remote_backup}")
            run(["scp", "-p", str(backup / "kiwi-recovery.tgz"), str(backup / "manifest.txt"),
                 f"{args.decoder_ssh}:{remote_backup}/"])
            stream_repository(repo, args.decoder_ssh, remote_source)
            remote_secret = f"/root/freedv-one-shot-secret-{stamp}.env"
            ssh(args.decoder_ssh, f"umask 077; cat > {remote_secret}", input_bytes=secret_content)
            action = "install" if args.install_remote_decoder else "configure"
            rade = "1" if args.enable_rade else "0"
            ssh(args.decoder_ssh, f"{remote_source}/deploy/install-remote-decoder.sh {remote_source} {remote_secret} {args.kiwi_ip} {action} {rade}")
        else:
            if args.backup_copy:
                run(["scp", "-p", str(backup / "kiwi-recovery.tgz"), str(backup / "manifest.txt"), args.backup_copy])
            run([str(repo / "deploy/install-ai64-local.sh"), str(repo)])
        decoder_prepared = True

        run([str(repo / "tools/apply-kiwi-overlay.sh"), str(repo), str(kiwi)])
        decoder_ip = "127.0.0.1" if args.mode == "local" else args.decoder_ip
        rade_option = "--enable-rade" if args.enable_rade else "--disable-rade"
        run([str(repo / "tools/configure-kiwi-freedv.py"), decoder_ip, rade_option])
        config_changed = True
        run([str(repo / "tools/run-kiwi-build.sh"), str(kiwi)])
        wait_build(args.build_timeout)
        candidate = kiwi.parent / "build/kiwid.bin"
        run([str(repo / "tools/verify-kiwi-candidate.sh"), args.release, str(kiwi), str(candidate)])
        zero_listener_gate()
        run([str(repo / "tools/deploy-kiwi-release.sh"), str(candidate.parent), args.release])
        deployed = True

        if args.mode == "local":
            set_env_value(
                Path("/etc/freedv-decoder/decoder.env"),
                "FREEDV_ENABLE_RADE",
                "1" if args.enable_rade else "0",
            )
            run(["systemctl", "start", "freedv-decoder.service", "freedv-reporter.service"])
        else:
            ssh(args.decoder_ssh, "systemctl start freedv-decoder.service freedv-reporter.service")
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline and not decoder_health(args):
            time.sleep(2)
        if not decoder_health(args):
            raise RuntimeError("decoder did not become healthy after Kiwi deployment")
        for sample in range(1, 4):
            current = status()
            if current.get("status") != "active" or not decoder_health(args):
                raise RuntimeError("post-deployment health sample failed")
            print(f"Post-deployment health sample {sample}/3 passed")
            if sample != 3:
                time.sleep(10)
        commit_prepared_decoder(args)
    except Exception:
        print("INSTALL FAILURE: starting automatic recovery", file=sys.stderr)
        stop_decoder(args)
        if deployed:
            run([str(repo / "tools/rollback-kiwi-release.sh"), previous])
        if config_changed:
            shutil.copy2(backup / "kiwi.json", "/root/kiwi.config/kiwi.json")
            run(["systemctl", "restart", "kiwid.service"])
        if deployed or config_changed:
            wait_kiwi_health()
        if decoder_prepared:
            rollback_prepared_decoder(args, remote_source)
        raise

    print("\nINSTALLATION PREPARATION PASSED")
    print(f"  active Kiwi release: {args.release}")
    print(f"  local recovery archive: {backup}")
    print("  Reporter remains disabled")
    print("Required before production: real-browser Test, live RADEV1 check and documented soak.")
    if args.mode == "local":
        print(f"Run: {repo}/tools/validate-ai64-local.sh 1800 while one RADEV1 session is active")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, RuntimeError, subprocess.CalledProcessError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
