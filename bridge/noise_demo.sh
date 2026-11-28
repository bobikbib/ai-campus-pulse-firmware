#!/usr/bin/env bash
# Noise demo: drives the simulated board through every noise level so you can
# watch the website change. Same C code as the board; audio is synthesized and
# analyzed on the "board" exactly like microphone samples.
#
#   export SUPABASE_KEY='sb_publishable_...'
#   bash bridge/noise_demo.sh [room board_key] [seconds per step] [supabase url]
#
# Open the site on that room (map/list/timeline panel) and watch the noise box.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOM="${1:-Z-3-03}"
STEP="${2:-20}"
URL="${3:-${SUPABASE_URL:-https://rymdfyoyeizpxgaxywjl.supabase.co}}"
: "${SUPABASE_KEY:?set SUPABASE_KEY to the publishable key}"
[ -x build/cp_sim ] || make -s

step() { printf '\n\033[1;36m=== %s ===\033[0m\n\033[0;36m    site should show: %s\033[0m\n' "$1" "$2" >&2; }

{
  echo "ROOM $ROOM"; echo "SIM 0"; echo "NOISE 38"; echo "VOICE 0"
  step "1/6  nobody in the room, 38 dB"          "Silent, nobody here  (grey)";        sleep "$STEP"
  echo "SIM 2"
  step "2/6  two people come in, still 38 dB"    "Quiet  (green), People now 2";       sleep "$STEP"
  echo "NOISE 42"; echo "VOICE 1"
  step "3/6  they start talking"                 "Noisy + Talking  (amber)";           sleep "$STEP"
  echo "VOICE 0"; echo "NOISE 60"
  step "4/6  no talking, but 60 dB background"   "Noisy  (amber), no Talking badge";   sleep "$STEP"
  echo "NOISE 80"
  step "5/6  80 dB"                              "Too loud  (red)";                    sleep "$STEP"
  echo "NOISE 38"; echo "SIM 0"
  step "6/6  everyone leaves, quiet again"       "Silent, nobody here  (grey)";        sleep "$STEP"
  step "done" "gateway keeps running for the phone; Ctrl+C to stop"
} | python3 bridge/board_gateway.py --supabase "$URL" --room "$ROOM" 2>&1 \
  | grep --line-buffered -E "===|site should|noise_level|gateway:" \
  | sed -u -E 's/.*"room":"([^"]*)","people_count":([0-9]+).*"noise_db":([0-9]+),"voice":([a-z]+),"noise_level":"([a-z_]+)".*/    board -> room \1: people=\2  noise=\3 dB  voice=\4  level=\5/'
