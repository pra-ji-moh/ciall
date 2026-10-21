#!/bin/sh
# build_phone.sh -- compile the being's life (brain/ob_life.c) to WebAssembly
# and put it inside the phone page: phone/index.html, one file, nothing else
# needed. Needs clang with the wasm32 target and wasm-ld.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
brain="$here/../brain"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
clang --target=wasm32 -std=c99 -Wall -Wextra -pedantic -Werror -O2 -nostdlib \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--strip-all \
  -o "$out/life.wasm" "$brain/ob_life.c" "$brain/ol_wasm.c"
base64 -w0 "$out/life.wasm" > "$out/life.b64"
awk -v f="$out/life.b64" 'BEGIN { getline wasm < f } { gsub(/__LIFE_WASM__/, wasm); print }' \
  "$here/crib.html" > "$out/page.html"
# the page as Claude publishes it (it adds its own skeleton) ...
cp "$out/page.html" "${ARTIFACT_OUT:-$out/artifact.html}"
# ... and as a page of its own, for any web address
{
  printf '<!doctype html><html lang="en"><head><meta charset="utf-8">'
  printf '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
  printf '<style>:root{padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}body{margin:0}img{max-width:100%%}[hidden]{display:none!important}</style>'
  printf '</head><body>\n'
  cat "$out/page.html"
  printf '</body></html>\n'
} > "$here/index.html"
echo "phone/index.html: $(wc -c < "$out/life.wasm") bytes of brain"
