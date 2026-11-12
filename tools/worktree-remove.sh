#!/usr/bin/env bash
# Removes a worktree that worktree-add.sh created, with its build tree.
# Usage: tools/worktree-remove.sh [--force] <path>
set -euo pipefail

force=()
if [[ ${1-} == --force ]]; then
    force=(--force)
    shift
fi
if [[ $# -ne 1 ]]; then
    echo "usage: $0 [--force] <path>" >&2
    exit 2
fi
path=$(realpath "$1")

if [[ $(git -C "$path" rev-parse --show-toplevel) != "$path" ]]; then
    echo "$path is not the root of a worktree" >&2
    exit 1
fi
git_dir=$(git -C "$path" rev-parse --absolute-git-dir)
common_dir=$(git -C "$path" rev-parse --path-format=absolute --git-common-dir)
if [[ $git_dir == "$common_dir" ]]; then
    echo "$path is the main worktree" >&2
    exit 1
fi
if [[ ! -f $git_dir/krr-disposable ]]; then
    echo "$path was not created by worktree-add.sh" >&2
    exit 1
fi
if [[ $(git rev-parse --show-toplevel) == "$path" ]]; then
    echo "$path is the current worktree" >&2
    exit 1
fi
# Commits made in the worktree stay in the repository.
git -C "$common_dir" worktree remove "${force[@]}" "$path"
