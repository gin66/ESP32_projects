#!/usr/bin/env python3
"""Tail the ESP32 PSRAM log ring over the WebSocket.

The firmware drains newline-delimited records to every connected client:

    [1234.567][I][tag] message

and prepends a {"dropped":N} frame when a client fell behind. The periodic
status JSON broadcast by the template is ignored unless --verbose is given.

Usage:
    python3 tools/tail_ws.py                    # ws://hauszentrale.local:81
    python3 tools/tail_ws.py 192.168.1.42
    python3 tools/tail_ws.py --save /tmp/esp.log
"""

import argparse
import asyncio
import json
import sys
import time

try:
    import websockets
except ImportError:
    websockets = None

RESET = "\033[0m"
COLORS = {
    "E": "\033[31m",   # red
    "W": "\033[33m",   # amber
    "I": "\033[37m",   # grey
    "D": "\033[90m",   # dim
}


def colorize(line: str) -> str:
    # [uptime][level][tag] msg
    if len(line) > 3 and line[0] == "[":
        end = line.find("][")
        if end > 0:
            level_at = end + 2
            if level_at < len(line) and line[level_at] in COLORS:
                color = COLORS[line[level_at]]
                return color + line + RESET
    return line


async def run(args):
    if websockets is None:
        sys.exit("websockets is required: python3 -m pip install websockets")
    url = f"ws://{args.host}:{args.port}"
    out = open(args.save, "a", encoding="utf-8") if args.save else None
    print(f"connected: {url}", file=sys.stderr)

    try:
        async with websockets.connect(url, ping_interval=20) as ws:
            async for message in ws:
                if isinstance(message, bytes):
                    message = message.decode("utf-8", "replace")

                # Control/status frames.
                try:
                    obj = json.loads(message)
                except (ValueError, TypeError):
                    obj = None

                if isinstance(obj, dict):
                    if "dropped" in obj:
                        print(f"\033[35m[reconnect gap: {obj['dropped']} bytes]\033[0m")
                        if out:
                            out.write(f"# dropped {obj['dropped']} bytes\n")
                        continue
                    if args.verbose:
                        print(json.dumps(obj))
                    continue

                for line in message.splitlines():
                    if not line:
                        continue
                    stamp = time.strftime("%H:%M:%S")
                    print(f"{stamp} {colorize(line)}")
                    if out:
                        out.write(f"{stamp} {line}\n")
    except KeyboardInterrupt:
        pass
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
    finally:
        if out:
            out.close()
            print(f"saved to {args.save}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Tail the ESP32 log ring")
    parser.add_argument("host", nargs="?", default="hauszentrale.local")
    parser.add_argument("--port", type=int, default=81)
    parser.add_argument("--save", metavar="FILE", help="append records to FILE")
    parser.add_argument("--verbose", action="store_true",
                        help="also print the template status JSON")
    args = parser.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
