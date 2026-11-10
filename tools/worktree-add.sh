#!/usr/bin/env bash
# Creates a disposable worktree that shares the selected preset's vcpkg packages.
# Usage: tools/worktree-add.sh [--preset <name>] <path> [<commit>]
set -euo pipefail

preset=developer
if [[ ${1-} == --preset && $# -ge 3 ]]; then
    preset=$2
    shift 2
fi

if [[ $# -lt 1 || $# -gt 2 || ${1-} == --preset ]]; then
    echo "usage: $0 [--preset <name>] <path> [<commit>]" >&2
    exit 2
fi
path=$1
commit=${2:-HEAD}

source_root=$(git rev-parse --show-toplevel)
build=$source_root/build/$preset
installed=$build/vcpkg_installed
cache=$build/CMakeCache.txt
toolchain=$source_root/external/vcpkg/scripts/buildsystems/vcpkg.cmake
if [[ ! -d $installed || ! -f $toolchain || ! -f $cache ]]; then
    echo "$source_root has no vcpkg packages: build its $preset preset first" >&2
    exit 1
fi
# The shared packages were installed from this worktree's manifest, so the commit must use the same.
if ! git diff --quiet HEAD "$commit" -- vcpkg.json vcpkg-configuration.json vcpkg external/vcpkg; then
    echo "$commit changes the vcpkg manifest, ports or triplets: build it in its own worktree" >&2
    exit 1
fi

git worktree add --detach "$path" "$commit"
# worktree-remove.sh reads this marker.
touch "$(git -C "$path" rev-parse --absolute-git-dir)/krr-disposable"

# Share the .scratch of the main worktree, which git lists first.
main=$(git worktree list --porcelain | sed -n "1s/^worktree //p")
mkdir -p "$main/.scratch"
ln -s "$main/.scratch" "$path/.scratch"

# vcpkg never runs in the new worktree. Its submodule stays empty.
options=(-DCMAKE_TOOLCHAIN_FILE="$toolchain" -DVCPKG_MANIFEST_INSTALL=OFF
         -DVCPKG_INSTALLED_DIR="$installed")
while IFS= read -r option; do
    options+=("$option")
done < <(sed -n 's/^\(KRR_[^:]*\):BOOL=/-D\1=/p' "$cache")
(cd "$path" && cmake --preset "$preset" "${options[@]}")
