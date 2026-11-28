#!/usr/bin/env bash
#
# Applies AI Campus Pulse to an Infineon PSOC Edge example. Run it in the
# Live Lab terminal.
#
#   tools/apply_to_example.sh <example_dir> [--room <board_key>] [--debug]
#
# Supports (auto-detected):
#   mtb-example-psoc-edge-ml-aihub-deploy-vision   (plan A, YOLACT people)
#   mtb-example-psoc-edge-ml-face-id               (plan B, faces)
#
# --debug  (aihub only) adds just a printf of the people count and doesn't copy
#          our code. This is for session 1, to verify the count.
# --room   the room this board reports, i.e. rooms.board_key in Supabase
#          (e.g. Z-3-04a). Default: ROOM-101. Letters, digits, - and _ only.
#
# Safe to run again: files already patched (marker AI_CAMPUS_PULSE) are skipped.
# To undo:  cd <example_dir> && git checkout . && rm -rf proj_cm55/campus_pulse
#
# Needs only bash, awk, cp, grep. That's enough for modus-shell on Windows and
# for Linux.

set -euo pipefail

MARK="AI_CAMPUS_PULSE"
HERE="$(cd "$(dirname "$0")/.." && pwd)"

die() { echo "ERROR: $*" >&2; exit 1; }
info() { echo "==> $*"; }

USAGE="usage: $0 <example_dir> [--room <board_key>] [--debug]"
[ $# -ge 1 ] || die "$USAGE"
EX="$(cd "$1" && pwd)" || die "no such directory: $1"
shift
DEBUG=0
ROOM=""
while [ $# -gt 0 ]; do
    case "$1" in
        --debug) DEBUG=1 ;;
        --room)  [ $# -ge 2 ] || die "$USAGE"; ROOM="$2"; shift ;;
        *)       die "$USAGE" ;;
    esac
    shift
done
if [ -n "$ROOM" ]; then
    printf '%s' "$ROOM" | grep -qE '^[A-Za-z0-9_-]{1,24}$' \
        || die "--room must be 1-24 letters, digits, - or _ (got '$ROOM')"
fi

# ---------------------------------------------------------------------------
# Detect which example this is
# ---------------------------------------------------------------------------
if [ -f "$EX/proj_cm55/source/inference_task.c" ] && \
   grep -q "ml_pipeline_post_process" "$EX/proj_cm55/source/inference_task.c"; then
    KIND=aihub
    HOOK_FILE="$EX/proj_cm55/source/inference_task.c"
    MAIN_FILE="$EX/proj_cm55/source/main.c"
elif [ -f "$EX/proj_cm55/lcd_task.c" ] && grep -q "ifx_face_id_inference(" "$EX/proj_cm55/lcd_task.c"; then
    KIND=faceid
    HOOK_FILE="$EX/proj_cm55/lcd_task.c"
    MAIN_FILE="$EX/proj_cm55/main.c"
else
    die "not a supported example (expected aihub-deploy-vision or face-id): $EX"
fi
info "example: $KIND"
[ $DEBUG -eq 1 ] && [ "$KIND" != aihub ] && die "--debug only applies to aihub-deploy-vision"
[ -f "$MAIN_FILE" ] || die "main.c not found: $MAIN_FILE"

# Insert TEXT after the first line matching REGEX (or after the end of the
# statement started on that line, if END_REGEX is given). Fails loudly if the
# anchor is missing, so a changed example version is noticed right away.
insert_after() {
    local file="$1" regex="$2" end_regex="$3" text="$4"
    if grep -q "$MARK" "$file" && grep -qF "$(printf '%s' "$text" | head -n1)" "$file"; then
        info "already patched: ${file#$EX/}"
        return
    fi
    grep -qE "$regex" "$file" || die "anchor '$regex' not found in ${file#$EX/} (example version changed?)"
    local tmp="$file.cp_tmp"
    TEXT="$text" RE="$regex" ENDRE="$end_regex" awk '
        BEGIN { state = 0; re = ENVIRON["RE"]; endre = ENVIRON["ENDRE"] }
        {
            print
            if (state == 0 && $0 ~ re) { state = (endre == "") ? 2 : 1 }
            if (state == 1 && $0 ~ endre) { state = 2 }
            if (state == 2) { print ENVIRON["TEXT"]; state = 3 }
        }
        END { if (state != 3) exit 3 }
    ' "$file" > "$tmp" || { rm -f "$tmp"; die "failed to patch ${file#$EX/}"; }
    mv "$tmp" "$file"
    info "patched: ${file#$EX/}"
}

insert_before() {
    local file="$1" regex="$2" text="$3"
    if grep -qF "$(printf '%s' "$text" | head -n1)" "$file"; then
        info "already patched: ${file#$EX/}"
        return
    fi
    grep -qE "$regex" "$file" || die "anchor '$regex' not found in ${file#$EX/}"
    local tmp="$file.cp_tmp"
    TEXT="$text" RE="$regex" awk '
        BEGIN { re = ENVIRON["RE"] }
        !done && $0 ~ re { print ENVIRON["TEXT"]; done = 1 }
        { print }
        END { if (!done) exit 3 }
    ' "$file" > "$tmp" || { rm -f "$tmp"; die "failed to patch ${file#$EX/}"; }
    mv "$tmp" "$file"
    info "patched: ${file#$EX/}"
}

# ---------------------------------------------------------------------------
# Session 1: debug printf only (aihub)
# ---------------------------------------------------------------------------
if [ $DEBUG -eq 1 ]; then
    insert_after "$HOOK_FILE" '#include "pipeline.h"' "" \
"/* $MARK debug */
#include <stdio.h>
extern int count_idx, start_index, end_index;"
    insert_after "$HOOK_FILE" 'ml_pipeline_post_process\(\);' "" \
"        { /* $MARK debug: verify people count */
            int cp_people = (count_idx == 0) ? 0 : (end_index - start_index);
            printf(\"DBG people=%d count_idx=%d start=%d end=%d\\n\",
                   cp_people, count_idx, start_index, end_index);
        }"
    info "done. Build: make build -j8 && make program"
    exit 0
fi

# ---------------------------------------------------------------------------
# Full integration: copy our sources
# ---------------------------------------------------------------------------
if grep -q "$MARK debug" "$HOOK_FILE"; then
    die "the debug patch is still applied; revert it first: (cd $EX && git checkout proj_cm55)"
fi
DST="$EX/proj_cm55/campus_pulse"
mkdir -p "$DST/core" "$DST/app" "$DST/port"
cp "$HERE"/src/core/*.c "$HERE"/src/core/*.h "$DST/core/"
cp "$HERE"/src/app/*.c  "$HERE"/src/app/*.h  "$DST/app/"
cp "$HERE"/port/pse84/cp_port.h "$HERE"/port/pse84/cp_port_pse84.c "$DST/port/"
info "copied sources to ${DST#$EX/}"
if [ -n "$ROOM" ]; then
    awk -v room="$ROOM" '
        /^#define CP_DEFAULT_ROOM_ID/ { sub(/"[^"]*"/, "\"" room "\"") }
        { print }' "$DST/core/cp_config.h" > "$DST/core/cp_config.h.tmp"
    mv "$DST/core/cp_config.h.tmp" "$DST/core/cp_config.h"
    grep -q "CP_DEFAULT_ROOM_ID *\"$ROOM\"" "$DST/core/cp_config.h" || die "failed to set room"
    info "room: $ROOM"
fi

# Hook in the inference loop
if [ "$KIND" = aihub ]; then
    insert_after "$HOOK_FILE" '#include "pipeline.h"' "" \
"/* $MARK */
#include \"cp_port.h\"
extern int count_idx, start_index, end_index;"
    insert_after "$HOOK_FILE" 'ml_pipeline_post_process\(\);' "" \
"        { /* $MARK: YOLACT keeps people in candidate_indices[start_index, end_index) */
            int cp_people = (count_idx == 0) ? 0 : (end_index - start_index);
            if (cp_people < 0) { cp_people = 0; }
            cp_port_on_detections((uint16_t)cp_people);
            cp_port_frame_gate();   /* camera asleep: wait for the next sample */
        }"
else
    insert_after "$HOOK_FILE" '^#include' "" \
"/* $MARK */
#include \"cp_port.h\""
    # The inference call spans several lines; insert after its closing ');'.
    insert_after "$HOOK_FILE" 'ifx_result = ifx_face_id_inference\(' '\);[[:space:]]*$' \
"            /* $MARK: report faces only when inference succeeded, so an
             * error never looks like an empty room. */
            if (ifx_result == IFX_FACEID_RSLT_SUCCESS) {
                cp_port_on_detections((uint16_t)((prediction.count > 0) ? prediction.count : 0));
            }
            cp_port_frame_gate();   /* camera asleep: wait for the next sample */"
fi

# Start our task before the scheduler
insert_after "$MAIN_FILE" '^#include' "" \
"/* $MARK */
#include \"cp_port.h\""
insert_before "$MAIN_FILE" 'vTaskStartScheduler\(\);' \
"    /* $MARK: start presence reporting task (static memory, no heap) */
    if (!cp_port_start()) { CY_ASSERT(0); }
"

info "done. Build: make build -j8 && make program"
info "Expect in Console Output: CP>{\"v\":1,\"type\":\"hello\",...}"
