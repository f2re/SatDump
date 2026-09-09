#!/usr/bin/env bash
# Source-only terminal primitives; Bash 4.x, no external TUI dependencies.
# Prompts go to stderr; answers are data, never evaluated as shell.
ui_init() {
    UI_COLOR=0 UI_MOTION=0 UI_PID='' UI_COMMAND_PID=''
    UI_RESET='' UI_TITLE='' UI_OK='' UI_WARN='' UI_BAD=''
    if [[ -t 2 && ${TERM:-dumb} != dumb ]]; then
        if [[ -z ${NO_COLOR+x} && ${COLOR:-auto} != never ]]; then
            UI_COLOR=1 UI_RESET=$'\033[0m' UI_TITLE=$'\033[1;36m'
            UI_OK=$'\033[32m' UI_WARN=$'\033[33m' UI_BAD=$'\033[31m'
        fi
        [[ ${ANIMATION:-auto} == never || -n ${CI:-} ]] || UI_MOTION=1
    fi
}
ui_cleanup() {
    if [[ -n ${UI_COMMAND_PID:-} ]]; then kill "$UI_COMMAND_PID" 2>/dev/null || true; wait "$UI_COMMAND_PID" 2>/dev/null || true; UI_COMMAND_PID=''; fi
    if [[ -n ${UI_PID:-} ]]; then kill "$UI_PID" 2>/dev/null || true; wait "$UI_PID" 2>/dev/null || true; UI_PID=''; fi
    if (( ${UI_MOTION:-0} )); then printf '\r\033[K\033[?25h' >&2; fi
}
ui_heading() { printf '\n%s%s%s\n' "$UI_TITLE" "$*" "$UI_RESET" >&2; }
ui_info() { printf '  %s\n' "$*" >&2; }
ui_error() { printf '%s  ОШИБКА: %s%s\n' "$UI_BAD" "$*" "$UI_RESET" >&2; }
ui_ask() {
    local question=$1 default=$2 answer
    printf '  %s [%s]: ' "$question" "$default" >&2
    if ! IFS= read -r answer; then return 20; fi
    case "$answer" in q|Q|й|Й) return 20 ;; b|B|н|Н) return 10 ;; esac
    UI_ANSWER=${answer:-$default}
}
ui_confirm() {
    local answer
    printf '\n  Применить этот план? [да/НЕТ]: ' >&2
    IFS= read -r answer || return 1
    [[ "$answer" == да || "$answer" == д || "$answer" == yes || "$answer" == y ]]
}
ui_step() {
    local title=$1 rc=0
    shift
    printf '  … %s\n' "$title" >&2
    if (( UI_MOTION )); then
        printf '\033[?25l' >&2
        ( while :; do
            for frame in '|' '/' '-' '\'; do printf '\r  %s %s' "$frame" "$title" >&2; sleep 0.12; done
          done ) &
        UI_PID=$!
    fi
    # Run independently so errexit inside the called script is not disabled by an if.
    "$@" >>"$INSTALL_LOG" 2>&1 &
    UI_COMMAND_PID=$!
    wait "$UI_COMMAND_PID" || rc=$?
    UI_COMMAND_PID=''
    ui_cleanup
    if (( rc )); then
        ui_error "$title (код $rc). Журнал: $INSTALL_LOG"
        tail -n 15 "$INSTALL_LOG" >&2
        return "$rc"
    fi
    printf '%s  ГОТОВО%s  %s\n' "$UI_OK" "$UI_RESET" "$title" >&2
}
