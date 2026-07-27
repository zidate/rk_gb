#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <RV1106_IPC_SDK-root>" >&2
    exit 2
fi

sdk_root=$(realpath -- "$1")
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ ! -f "$sdk_root/project/build.sh" ]; then
    echo "error: missing SDK build script: $sdk_root/project/build.sh" >&2
    exit 1
fi

while IFS= read -r patch_name || [ -n "$patch_name" ]; do
    test -n "$patch_name" || continue
    patch -d "$sdk_root" -p1 --forward < "$script_dir/$patch_name"
done < "$script_dir/series"
