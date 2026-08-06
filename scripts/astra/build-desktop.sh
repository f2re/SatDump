#!/usr/bin/env bash
set -Eeuo pipefail
exec bash "$(dirname "$0")/build.sh" --mode native --profile desktop --install
