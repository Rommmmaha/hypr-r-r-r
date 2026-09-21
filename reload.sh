#!/usr/bin/env bash
# this script is for testing only
set -euo pipefail

usage() {
  echo "usage: $0 <load|restore> <plugin-name>" >&2
  echo "example: $0 load hyprstretch" >&2
  exit 1
}

if [ "$#" -ne 2 ]; then
  usage
fi

cmd="$1"
PLUGIN_NAME="$2"

case "$PLUGIN_NAME" in
  "" | */* | .* | *" "*)
    echo "error: invalid plugin name: '$PLUGIN_NAME'" >&2
    exit 1
    ;;
esac

if [ ! -d "$PLUGIN_NAME" ]; then
  echo "error: plugin directory not found: '$PLUGIN_NAME'" >&2
  exit 1
fi

ABS_SO="$(pwd)/$PLUGIN_NAME/$PLUGIN_NAME.so"
HYPRPM_SO="/var/cache/hyprpm/$USER/$PLUGIN_NAME/$PLUGIN_NAME.so"

case "$cmd" in
  load)
    make -C "$PLUGIN_NAME" all
    hyprctl plugin unload "$ABS_SO" || true
    hyprctl plugin unload "$HYPRPM_SO" || true
    hyprctl plugin load "$ABS_SO"
    ;;
  restore)
    hyprctl plugin unload "$ABS_SO" || true
    hyprctl plugin unload "$HYPRPM_SO" || true
    hyprpm reload
    ;;
  *)
    usage
    ;;
esac
