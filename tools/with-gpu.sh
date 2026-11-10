#!/usr/bin/env bash
# Runs a command while it holds the lock of one GPU, so that GPU jobs never overlap.
# The GPU is CUDA_VISIBLE_DEVICES, or 0. The command waits for the lock, and holds it until it and its children exit.
# Usage: tools/with-gpu.sh <command> [<argument>...]
#        tools/with-gpu.sh --who
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <command> [<argument>...] | --who" >&2
    exit 2
fi
gpu=${CUDA_VISIBLE_DEVICES:-0}
# The lock covers one GPU, so it cannot cover a list of them.
if [[ ! $gpu =~ ^[0-9]+$ ]]; then
    echo "CUDA_VISIBLE_DEVICES must name one GPU, not '$gpu'" >&2
    exit 2
fi

# The lock file also records its holder.
lock=${XDG_RUNTIME_DIR:-/tmp}/kirara-gpu-$gpu.lock
exec 9>>"$lock"

if [[ $1 == --who ]]; then
    if flock --nonblock 9; then
        echo "GPU $gpu is free"
    else
        echo "GPU $gpu is held by $(cat "$lock")"
    fi
    exit 0
fi

if ! flock --nonblock 9; then
    echo "waiting for GPU $gpu, held by $(cat "$lock")" >&2
    flock 9
fi
printf 'pid %s in %s since %s: %s\n' "$$" "$PWD" "$(date '+%F %T')" "$*" >"$lock"

export CUDA_VISIBLE_DEVICES=$gpu
exec "$@"
