#!/bin/bash
# Headless display + audio for bot runs on a box with no desktop (source it: sets DISPLAY and the speed-up).
# OpenAL's null/wave backends crash MonoGame here, so a pulseaudio daemon stands in for a sound card.
pgrep -x Xvfb >/dev/null || (Xvfb :99 -screen 0 640x480x24 +extension GLX -nolisten tcp >/dev/null 2>&1 &)
pactl info >/dev/null 2>&1 || pulseaudio -D --exit-idle-time=-1 2>/dev/null
export DISPLAY=:99
# Software GL is most of the frame time: draw one frame in 60 (traces are identical, screenshots show the last drawn frame).
export ANODYNE_BOT_DRAW_EVERY=${ANODYNE_BOT_DRAW_EVERY:-60}
