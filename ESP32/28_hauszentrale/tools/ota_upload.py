#!/usr/bin/env python3
"""PlatformIO custom uploader for the panel's ArduinoOTA server.

PlatformIO's bundled Python (penv) does not complete the espota callback on
this Mac, so delegate to the system Python, which does. The host address is
detected from en0; override it with HAUS_OTA_HOST_IP if needed.
"""
import os
import subprocess
import sys


def lan_ip():
    env = os.environ.get("HAUS_OTA_HOST_IP")
    if env:
        return env
    for iface in ("en0", "en1"):
        try:
            ip = subprocess.check_output(
                ["ipconfig", "getifaddr", iface], text=True
            ).strip()
            if ip:
                return ip
        except (OSError, subprocess.CalledProcessError):
            pass
    return None


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else ""
    target = sys.argv[2] if len(sys.argv) > 2 else ""
    if not target or target.startswith("$"):
        target = "hauszentrale.fritz.box"

    espota = os.path.expanduser(
        "~/.platformio/packages/framework-arduinoespressif32/tools/espota.py"
    )
    cmd = [sys.executable, espota, "--debug", "--progress",
           "--port=3232", "-i", target, "-f", source]
    host_ip = lan_ip()
    if host_ip:
        cmd += ["--host_ip", host_ip]
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
