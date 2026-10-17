#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Build the standalone x86_64 MT7922 kext and its menu application.
set -euo pipefail
cd "$(dirname "$0")/.."
kernel_sdk="${MACKERNELSDK:-build/MacKernelSDK}"
sdk_commit=3f750085caa17ec3a7880f11c11bf4f48cd6a164
if [[ ! -d "$kernel_sdk/Headers" ]]; then
    if [[ -n "${MACKERNELSDK:-}" ]]; then
        echo "MACKERNELSDK must point to a complete MacKernelSDK checkout" >&2
        exit 1
    fi
    mkdir -p build
    git clone --filter=blob:none https://github.com/acidanthera/MacKernelSDK "$kernel_sdk"
    git -C "$kernel_sdk" checkout --detach "$sdk_commit"
fi
(cd fw && shasum -a 256 -c SHA256SUMS)
apple_sdk="$(xcrun --show-sdk-path)"
link_min="$(otool -l "$apple_sdk/usr/lib/libkmodc++.a" | awk '/minos/ && !v {v=$2} END {print v}')"
link_min="${link_min:-12.0}"
common=(-arch x86_64 -mmacosx-version-min=12.0 -O2 -g -fno-omit-frame-pointer
        "-ffile-prefix-map=$PWD=." "-fdebug-prefix-map=$PWD=."
        "-ffile-prefix-map=$kernel_sdk=MacKernelSDK" "-fdebug-prefix-map=$kernel_sdk=MacKernelSDK"
        -mkernel -fno-builtin -fno-stack-protector -nostdlib -nostdlibinc
        -ffreestanding -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
        "-I$kernel_sdk/Headers" -Isrc -Icodec)
kext=build/itlmt7922.kext
mkdir -p build/obj "$kext/Contents/MacOS"
cp src/Info.plist "$kext/Contents/Info.plist"
objects=()
sources=(itlmt7922 itlmt7922_m1 itlmt7922_m2 itlmt7922_m3 itlmt7922_txrx
         itlmt7922_pwr itlmt7922_ctl itlmt7922_eth itlmt7922_ba itlmt7922_uc
         itl_wpa itl_caps itl_ba)
for name in "${sources[@]}"; do
    object="build/obj/$name.o"
    xcrun clang++ "${common[@]}" -std=c++17 -fapple-kext -fno-exceptions -fno-rtti \
        -c "src/$name.cpp" -o "$object"
    objects+=("$object")
done
for source in codec/*.cpp; do
    object="build/obj/$(basename "${source%.cpp}").o"
    xcrun clang++ "${common[@]}" -std=c++17 -fapple-kext -fno-exceptions -fno-rtti \
        -c "$source" -o "$object"
    objects+=("$object")
done
for name in kmod_info fw_blob; do
    object="build/obj/$name.o"
    xcrun clang "${common[@]}" -c "src/$name.c" -o "$object"
    objects+=("$object")
done
xcrun clang++ -arch x86_64 "-mmacosx-version-min=$link_min" -nostdlib \
    -Xlinker -kext -Xlinker -no_data_const \
    "-L$kernel_sdk/Library/x86_64" "-L$apple_sdk/usr/lib" \
    -lkmodc++ -lkmod -lcc_kext "${objects[@]}" -o "$kext/Contents/MacOS/itlmt7922"
# Keep runtime symbols; remove debug-only object-file anchors containing build paths.
xcrun strip -S "$kext/Contents/MacOS/itlmt7922"
plutil -lint "$kext/Contents/Info.plist"
file "$kext/Contents/MacOS/itlmt7922"
app=build/ItlWiFi7922.app
mkdir -p "$app/Contents/MacOS" build/swift-module-cache
cp app/Info.plist "$app/Contents/Info.plist"
xcrun swiftc -O -target x86_64-apple-macos13.0 \
    -debug-prefix-map "$PWD=." \
    -module-cache-path "$PWD/build/swift-module-cache" \
    app/ItlWiFi7922.swift app/SpeedTest.swift \
    -o "$app/Contents/MacOS/ItlWiFi7922" -framework IOKit -framework SwiftUI
codesign --force --sign - "$app"
codesign --verify --deep --strict "$app"
file "$app/Contents/MacOS/ItlWiFi7922"
