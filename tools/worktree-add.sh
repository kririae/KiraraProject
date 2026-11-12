#!/usr/bin/env bash
# Creates a disposable worktree whose developer build reads the vcpkg packages of this worktree.
# Usage: tools/worktree-add.sh <path> [<commit>]
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 <path> [<commit>]" >&2
    exit 2
fi
path=$1
commit=${2:-HEAD}

source_root=$(git rev-parse --show-toplevel)
installed=$source_root/build/developer/vcpkg_installed
toolchain=$source_root/external/vcpkg/scripts/buildsystems/vcpkg.cmake
if [[ ! -d $installed || ! -f $toolchain ]]; then
    echo "$source_root has no vcpkg packages: build its developer preset first" >&2
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
cache=$source_root/build/developer/CMakeCache.txt
for option in KRR_BUILD_FLUX_HYDRA KRR_BUILD_KIRARA_DANCE; do
    value=$(sed -n "s/^$option:BOOL=//p" "$cache")
    if [[ -n $value ]]; then options+=("-D$option=$value"); fi
done
(cd "$path" && cmake --preset developer "${options[@]}")
