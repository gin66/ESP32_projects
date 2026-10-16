#!/bin/bash
# Render mockup.html to 480×800 PNGs in this directory.
#
#   ./render.sh            # every screen
#   ./render.sh home power # only the named screens
#
# A screen is a <section id="…" class="screen"> in mockup.html. The page is
# opened as file://…/mockup.html?s=<id>.
#
# Requires Google Chrome (or set CHROME to the binary). The page loads
# Montserrat from jsDelivr; the first run needs a network. Do not commit
# those font files. If the fetch fails, the page falls back to Avenir Next
# or Helvetica Neue and the PNGs still render.
#
# --force-device-scale-factor=1 keeps the file at 480×800 on a retina
# display. --virtual-time-budget waits for the remote fonts before the shot.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
cd "$here"

find_chrome() {
  if [[ -n "${CHROME:-}" && -x "$CHROME" ]]; then
    printf '%s\n' "$CHROME"
    return
  fi
  local c name
  for c in \
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
    "/Applications/Chromium.app/Contents/MacOS/Chromium" \
    "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary"
  do
    if [[ -x "$c" ]]; then
      printf '%s\n' "$c"
      return
    fi
  done
  for name in google-chrome chromium chromium-browser; do
    if c=$(command -v "$name" 2>/dev/null) && [[ -n "$c" ]]; then
      printf '%s\n' "$c"
      return
    fi
  done
  return 1
}

if ! chrome=$(find_chrome); then
  echo "Google Chrome not found. Set CHROME to the binary." >&2
  exit 1
fi

if [[ $# -gt 0 ]]; then
  screens=("$@")
else
  screens=()
  while IFS= read -r id; do
    screens+=("$id")
  done < <(sed -n 's/.*<section id="\([^"]*\)" class="screen".*/\1/p' mockup.html)
fi

if [[ ${#screens[@]} -eq 0 ]]; then
  echo "No screens found in mockup.html." >&2
  exit 1
fi

profile=$(mktemp -d)
trap 'rm -rf "$profile"' EXIT

for s in "${screens[@]}"; do
  out="${here}/${s}.png"
  "$chrome" \
    --headless=new \
    --disable-gpu \
    --hide-scrollbars \
    --force-device-scale-factor=1 \
    --window-size=480,800 \
    --user-data-dir="$profile" \
    --no-first-run \
    --disable-extensions \
    --virtual-time-budget=8000 \
    --screenshot="$out" \
    "file://${here}/mockup.html?s=${s}" \
    >/dev/null 2>&1
  python3 -c 'import struct,sys; w,h=struct.unpack(">II", open(sys.argv[1],"rb").read()[16:24]); assert (w,h)==(480,800), (sys.argv[1], w, h)' "$out"
  echo "$s.png 480x800"
done
