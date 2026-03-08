#!/usr/bin/env python3
"""
bambu_fw_upgrade.py — send upgrade_confirm MQTT command to a Bambu Lab printer over LAN.

Usage:
    python3 bambu_fw_upgrade.py [--printer <hostname>] [ip] [serial] [access_code] [manifest_url] [version]

    --printer  Full printer hostname (e.g. bambu-a1-printer.shellware.com).
               The hostname label (e.g. bambu-a1-printer) is used as the secret key prefix:
               <label>_serial, <label>_access_code, <label>_fw_manifest_url, <label>_fw_version
               The hostname itself is used as the MQTT host unless overridden by positional [ip].
    All positional args override the corresponding secret.

    Update a stored value:
        python3 ../bambu-printer-manager/secrets.py set bambu-a1-printer_access_code <NEW_CODE>

Examples:
    python3 bambu_fw_upgrade.py --printer bambu-a1-printer.shellware.com
    python3 bambu_fw_upgrade.py --printer bambu-h2d-printer.shellware.com

The printer downloads the manifest and components from the CDN, verifies BIMH signatures,
and applies the update. Works in LAN mode with outbound internet access (CDN is public).

Requirements:
    pip install paho-mqtt
"""

import json
import os
import ssl
import sys
import time

import paho.mqtt.client as mqtt

# secrets.py lives alongside this script (symlinked from bambu-printer-manager)
sys.path.insert(0, os.path.dirname(__file__))
from secrets import _keychain_get, _svc  # type: ignore # noqa: E402


def _secret(key: str) -> str | None:
    return _keychain_get(_svc(key))


def send_upgrade(host: str, serial: str, access_code: str, url: str, version: str) -> None:
    cmd = {
        "upgrade": {
            "sequence_id": "0",
            "command": "upgrade_confirm",
            "src_id": 2,
            "url": url,
            "module": "ota",
            "version": version,
            "reason": "",
            "result": "success",
            "upgrade_type": 4,
        }
    }

    done = [False]

    def on_connect(client, _userdata, _flags, _rc, _props=None):
        topic = f"device/{serial}/request"
        payload = json.dumps(cmd)
        client.publish(topic, payload)
        print(f"[+] Published upgrade_confirm to {topic}")
        print(f"    version : {version}")
        print(f"    url     : {url}")
        done[0] = True

    def on_publish(client, _userdata, _mid, *_args):
        time.sleep(1)
        client.disconnect()

    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2) # type: ignore
    c.tls_set(cert_reqs=ssl.CERT_NONE)
    c.tls_insecure_set(True)
    c.username_pw_set("bblp", access_code)
    c.on_connect = on_connect
    c.on_publish = on_publish

    print(f"[*] Connecting to {host}:8883 (serial={serial}) ...")
    c.connect(host, 8883, keepalive=10)
    c.loop_forever()

    if done[0]:
        print("[+] Done. Watch the printer panel for the update progress bar.")
    else:
        print("[!] Connection failed — check IP, serial, and access code.")
        sys.exit(1)


if __name__ == "__main__":
    args = sys.argv[1:]

    if args and args[0] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)

    # --printer flag — accepts full hostname e.g. bambu-a1-printer.shellware.com
    hostname = None
    if "--printer" in args:
        idx = args.index("--printer")
        if idx + 1 >= len(args):
            print("[!] --printer requires a hostname (e.g. bambu-a1-printer.shellware.com)")
            sys.exit(1)
        hostname = args[idx + 1]
        args = args[:idx] + args[idx + 2:]

    # --url override
    url_override = None
    if "--url" in args:
        idx = args.index("--url")
        if idx + 1 >= len(args):
            print("[!] --url requires a value")
            sys.exit(1)
        url_override = args[idx + 1]
        args = args[:idx] + args[idx + 2:]

    # --dry-run flag
    dry_run = "--dry-run" in args
    if dry_run:
        args = [a for a in args if a != "--dry-run"]

    if not hostname and not args:
        print("[!] --printer <hostname> is required (e.g. bambu-a1-printer.shellware.com)")
        sys.exit(1)

    # Derive secret prefix: strip domain suffix, use hostname label as key prefix
    p = hostname.split(".")[0] if hostname else None

    _host   = args[0] if len(args) > 0 else (hostname or _secret(f"{p}_ip"))
    _serial = args[1] if len(args) > 1 else _secret(f"{p}_serial")
    _code   = args[2] if len(args) > 2 else _secret(f"{p}_access_code")
    _url    = url_override or (args[3] if len(args) > 3 else _secret(f"{p}_fw_manifest_url"))
    _ver    = args[4] if len(args) > 4 else _secret(f"{p}_fw_version")

    missing = [k for k, v in [("serial", _serial), ("access_code", _code),
                               ("manifest_url", _url), ("version", _ver)] if not v]
    if missing:
        print(f"[!] Missing values for '{p}': {', '.join(missing)}")
        print(f"    Store them with: python3 secrets.py set {p}_<key> <value>")
        sys.exit(1)

    if dry_run:
        print(f"[dry-run] Would connect to {_host}:8883 (serial={_serial})")
        print(f"[dry-run] upgrade.start  version={_ver}")
        print(f"[dry-run] url={_url}")
        sys.exit(0)

    # ⚠️  MANDATORY CONFIRMATION — never bypass this block
    print()
    print(f"  ⚠️  WARNING: This will flash firmware on a physical printer.")
    print(f"      Printer : {_host}")
    print(f"      Serial  : {_serial}")
    print(f"      Version : {_ver}")
    print()
    try:
        answer = input("  Type YES to confirm, anything else to abort: ").strip()
    except (EOFError, KeyboardInterrupt):
        print("\n[!] Aborted.")
        sys.exit(1)
    if answer != "YES":
        print("[!] Aborted.")
        sys.exit(1)
    print()

    send_upgrade(_host, _serial, _code, _url, _ver)
