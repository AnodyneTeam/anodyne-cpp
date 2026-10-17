#!/bin/bash
# Starts a private virtual X server (works while the desktop is locked / on another VT). Prints the DISPLAY to use.
# Needs Xvfb: either installed, or unpacked locally with: apt-get download xvfb && dpkg -x xvfb_*.deb ~/xvfb-local/root
XVFB=$(command -v Xvfb || echo "$HOME/xvfb-local/root/usr/bin/Xvfb")
D=${1:-:99}
if ! xdpyinfo -display "$D" >/dev/null 2>&1; then
    "$XVFB" "$D" -screen 0 640x480x24 +extension GLX -nolisten tcp >/dev/null 2>&1 &
    sleep 2
fi
echo "$D"
