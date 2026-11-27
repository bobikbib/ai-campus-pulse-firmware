#!/usr/bin/env bash
# Downloads the MediaPipe browser bundles and the two .tflite models into web/.
# Idempotent: files that already exist (non-empty) are skipped.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER=1.2.2
CDN="https://cdn.jsdelivr.net/npm/@mediapipe"
GCS="https://storage.googleapis.com/mediapipe-models"

fetch() { # url dest
  local url="$1" dest="$2"
  if [ -s "$dest" ]; then echo "skip  ${dest#$ROOT/}"; return; fi
  mkdir -p "$(dirname "$dest")"
  echo "get   ${dest#$ROOT/}"
  curl -fL --retry 3 -sS -o "$dest.part" "$url"
  [ -s "$dest.part" ] || { echo "empty download: $url" >&2; rm -f "$dest.part"; exit 1; }
  mv "$dest.part" "$dest"
}

V="$ROOT/web/vendor"
fetch "$CDN/tasks-vision@$VER/vision_bundle.mjs"            "$V/tasks-vision/vision_bundle.mjs"
fetch "$CDN/tasks-vision@$VER/wasm/vision_wasm_internal.js"   "$V/tasks-vision/wasm/vision_wasm_internal.js"
fetch "$CDN/tasks-vision@$VER/wasm/vision_wasm_internal.wasm" "$V/tasks-vision/wasm/vision_wasm_internal.wasm"
fetch "$CDN/tasks-audio@$VER/audio_bundle.mjs"              "$V/tasks-audio/audio_bundle.mjs"
fetch "$CDN/tasks-audio@$VER/wasm/audio_wasm_internal.js"     "$V/tasks-audio/wasm/audio_wasm_internal.js"
fetch "$CDN/tasks-audio@$VER/wasm/audio_wasm_internal.wasm"   "$V/tasks-audio/wasm/audio_wasm_internal.wasm"

M="$ROOT/web/models"
fetch "$GCS/object_detector/efficientdet_lite0/int8/1/efficientdet_lite0.tflite" "$M/efficientdet_lite0.tflite"
fetch "$GCS/audio_classifier/yamnet/float32/1/yamnet.tflite"                     "$M/yamnet.tflite"

echo; echo "Sizes (bytes):"
find "$V" "$M" -type f -exec wc -c {} + | sed "s#$ROOT/##"
