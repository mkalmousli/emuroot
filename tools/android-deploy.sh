#!/bin/sh
# Pushes the right emuroot build to a connected Android device/emulator and checks it.
#
#   tools/android-deploy.sh [-s SERIAL] [BUILD_DIR]     (BUILD_DIR defaults to ./build)
#
# Android 5+ gets the NDK build (bionic, PIE); older Android gets the static build, which
# needs only the kernel and runs on any version. Build both first:
#   ./build.py --ndk && ./build.py --musl
set -e
ADB=adb
if [ "$1" = "-s" ]; then ADB="adb -s $2"; shift 2; fi
BUILD=${1:-build}

abi=$($ADB shell getprop ro.product.cpu.abi | tr -d '\r')
api=$($ADB shell getprop ro.build.version.sdk | tr -d '\r')
if [ "$api" -ge 21 ]; then
    case "$abi" in
        arm64-v8a) dir=android-arm64 ;; armeabi-v7a) dir=android-armv7 ;;
        x86_64) dir=android-x86_64 ;; x86) dir=android-x86 ;;
        *) echo "unsupported ABI $abi" >&2; exit 1 ;;
    esac
else
    case "$abi" in
        armeabi-v7a|armeabi) dir=armv5-musl ;; x86) dir=x86-musl ;;
        *) echo "unsupported ABI $abi (API $api)" >&2; exit 1 ;;
    esac
fi
bin=$BUILD/$dir/bin/emuroot
[ -f "$bin" ] || { echo "$bin not found - build it first" >&2; exit 1; }

echo "device: Android API $api, $abi  ->  $dir"
$ADB push "$bin" /data/local/tmp/emuroot >/dev/null
$ADB shell chmod 755 /data/local/tmp/emuroot
$ADB shell /data/local/tmp/emuroot -D
echo "installed at /data/local/tmp/emuroot"
