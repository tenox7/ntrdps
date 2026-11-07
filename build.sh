#!/bin/sh
# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.
#
# Runs the nmake makefiles in Docker: tenox7/nt31sdk, tenox7/nt35sdk, tenox7/nt40sdk.
# Usage: [DDK=<folder with nt31/DDK, nt351/DDK, nt40/ddk>] ./build.sh [target ...]
# Targets: nt31 nt35 nt40, with DDK also nt31ddk nt351ddk nt40ddk and setup
# (the installer; it packs the files of the others). Output: out/<target>/

cd "$(dirname "$0")" || exit 1
ROOT=$(pwd)
if [ $# -eq 0 ]; then
    set -- nt31 nt35 nt40
    if [ -n "$DDK" ]; then set -- "$@" nt31ddk nt351ddk nt40ddk setup; fi
fi
rc=0
for t in "$@"; do
    case $t in
    nt31)     img=nt31sdk; dir=src;         ddk= ;;
    nt35)     img=nt35sdk; dir=src;         ddk= ;;
    nt40)     img=nt40sdk; dir=src;         ddk= ;;
    nt31ddk)  img=nt31sdk; dir=src/drivers; ddk=nt31/DDK ;;
    nt351ddk) img=nt35sdk; dir=src/drivers; ddk=nt351/DDK ;;
    nt40ddk)  img=nt40sdk; dir=src/drivers; ddk=nt40/ddk ;;
    setup)    img=nt31sdk; dir=src;         ddk= ;;
    *) echo "build.sh: unknown target $t" >&2; exit 2 ;;
    esac
    if [ -n "$ddk" ] && [ ! -d "$DDK/$ddk" ]; then
        echo "build.sh: $t needs \$DDK/$ddk" >&2; rc=1; continue
    fi
    work=$ROOT/.build/$t
    mkdir -p "$work" && find "$work" -mindepth 1 -delete && cp -R "$dir/." "$work/" || exit 1
    if [ "$t" = setup ]; then
        ok=1
        mkdir -p "$work/setup/nt31" "$work/setup/nt35" "$work/setup/nt40" || exit 1
        for f in nt31/ntrdps.exe:nt31 nt31/ntrdpshk.dll:nt31 nt31ddk/ntrdpsly.dll:nt31 \
                 nt35/ntrdps.exe:nt35 nt35/ntrdpshk.dll:nt35 nt351ddk/ntrdpsly.dll:nt35 \
                 nt40ddk/ntrdpsmr.sys:nt40 nt40ddk/ntrdpsmr.dll:nt40 nt40ddk/ntrdpsm0.dll:nt40; do
            if [ -f "out/${f%%:*}" ]; then
                cp "out/${f%%:*}" "$work/setup/${f##*:}/" || exit 1
            else
                echo "build.sh: setup needs out/${f%%:*}" >&2; ok=0
            fi
        done
        if [ $ok = 0 ]; then rc=1; continue; fi
    fi
    echo "=== $t"
    if [ -n "$ddk" ]; then
        docker run --rm --network none --platform linux/386 -v "$work:/src" \
            -v "$DDK/$ddk:/ddk:ro" "tenox7/$img" nmake -nologo -f "$t.mak" "DDKROOT=Z:\\ddk" || rc=1
    else
        docker run --rm --network none --platform linux/386 -v "$work:/src" \
            "tenox7/$img" nmake -nologo -f "$t.mak" || rc=1
    fi
    mkdir -p "out/$t"
    for f in "$work"/*.exe "$work"/*.dll "$work"/*.sys; do
        if [ -f "$f" ]; then cp "$f" "out/$t/"; fi
    done
done
exit $rc
