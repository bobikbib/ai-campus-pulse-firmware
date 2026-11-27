#!/usr/bin/env bash
# Downloads the MediaPipe browser bundles and the two .tflite models into web/.
# Idempotent: files that already exist (non-empty; .tflite also with a valid "TFL3" header) are skipped.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER=1.2.2
CDN="https://cdn.jsdelivr.net/npm/@mediapipe"
GCS="https://storage.googleapis.com/mediapipe-models"

is_tflite() { # file: TFLite flatbuffers carry the identifier "TFL3" at byte offset 4
  [ "$(dd if="$1" bs=1 skip=4 count=4 2>/dev/null)" = "TFL3" ]
}

download() { # url dest
  mkdir -p "$(dirname "$2")"
  curl -fL --retry 3 -sS -o "$2.part" "$1"
  [ -s "$2.part" ] || { echo "empty download: $1" >&2; rm -f "$2.part"; exit 1; }
  mv "$2.part" "$2"
}

fetch() { # url dest [tflite]
  local url="$1" dest="$2" kind="${3:-}"
  if [ -s "$dest" ]; then
    if [ "$kind" != tflite ] || is_tflite "$dest"; then echo "skip  ${dest#$ROOT/}"; return; fi
    echo "bad   ${dest#$ROOT/} is not a TFLite model (captive portal / error page?), re-downloading"
    rm -f "$dest"
  fi
  echo "get   ${dest#$ROOT/}"
  download "$url" "$dest"
  if [ "$kind" = tflite ] && ! is_tflite "$dest"; then
    echo "bad   ${dest#$ROOT/} still not TFLite, retrying once"
    rm -f "$dest"
    download "$url" "$dest"
    if ! is_tflite "$dest"; then
      rm -f "$dest"
      echo "ERROR: $url did not return a TFLite model (check network / proxy / login page)" >&2
      exit 1
    fi
  fi
}

V="$ROOT/web/vendor"
fetch "$CDN/tasks-vision@$VER/vision_bundle.mjs"            "$V/tasks-vision/vision_bundle.mjs"
fetch "$CDN/tasks-vision@$VER/wasm/vision_wasm_internal.js"   "$V/tasks-vision/wasm/vision_wasm_internal.js"
fetch "$CDN/tasks-vision@$VER/wasm/vision_wasm_internal.wasm" "$V/tasks-vision/wasm/vision_wasm_internal.wasm"
fetch "$CDN/tasks-audio@$VER/audio_bundle.mjs"              "$V/tasks-audio/audio_bundle.mjs"
fetch "$CDN/tasks-audio@$VER/wasm/audio_wasm_internal.js"     "$V/tasks-audio/wasm/audio_wasm_internal.js"
fetch "$CDN/tasks-audio@$VER/wasm/audio_wasm_internal.wasm"   "$V/tasks-audio/wasm/audio_wasm_internal.wasm"

M="$ROOT/web/models"
fetch "$GCS/object_detector/efficientdet_lite0/int8/1/efficientdet_lite0.tflite" "$M/efficientdet_lite0.tflite" tflite
fetch "$GCS/audio_classifier/yamnet/float32/1/yamnet.tflite"                     "$M/yamnet.tflite" tflite

echo; echo "Sizes (bytes):"
find "$V" "$M" -type f -exec wc -c {} + | sed "s#$ROOT/##"
