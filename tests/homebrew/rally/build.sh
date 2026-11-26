#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
aarch64-none-elf-gcc -std=gnu17 -O2 -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -I"$DEVKITPRO/libnx/include" -specs="$DEVKITPRO/libnx/switch.specs" main.c -L"$DEVKITPRO/libnx/lib" -lnx -o rally.elf
nacptool --create "Rally" "SwitchCore" "0.1.0" rally.nacp
elf2nro rally.elf rally.nro --nacp=rally.nacp

