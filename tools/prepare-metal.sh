#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-metal
archive=build-metal/MoltenVK-all.tar
if [[ ! -f "$archive" ]]; then
  curl --fail --location --retry 3 https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-all.tar -o "$archive"
fi
echo "562a15a29bc358446a56a4091c5f7e08f604184187c1d34f712148b61ef17276  $archive" | shasum -a 256 -c -
tar -xf "$archive" -C build-metal MoltenVK/MoltenVK/static/MoltenVK.xcframework MoltenVK/MoltenVK/include MoltenVK/LICENSE

