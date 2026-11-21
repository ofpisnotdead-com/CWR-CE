#!/bin/sh
set -eu

out=$(realpath -m "$1")
engine=${CONTAINER_ENGINE:-docker}

"$engine" build --platform linux/ppc64le -t cwr-ppc64le-sysroot "$(dirname "$0")"
rm -rf "$out"
mkdir -p "$out/cwr-pkgconfig"
id=$("$engine" create --platform linux/ppc64le cwr-ppc64le-sysroot)
"$engine" export "$id" | tar -x -C "$out" --exclude='dev/*'
"$engine" rm "$id" >/dev/null

for pc in "$out"/usr/lib/powerpc64le-linux-gnu/pkgconfig/*.pc "$out"/usr/share/pkgconfig/*.pc; do
    sed -E "s#^(prefix|exec_prefix|libdir|includedir)=/#\1=$out/#" "$pc" > "$out/cwr-pkgconfig/${pc##*/}"
done
