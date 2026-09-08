#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
exec bash "$ROOT/scripts/station/install.sh" "$@"
