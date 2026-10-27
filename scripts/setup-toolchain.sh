#!/usr/bin/env bash
# Downloads the Microsoft CRT and Windows SDK for the Linux cross-build into build/toolchains/,
# using a pinned, checksum-verified xwin release. Needed once before scripts/build-arch.sh.
set -euo pipefail

if [[ "${1:-}" != "--accept-license" ]]; then
    echo "Usage: $0 --accept-license" >&2
    echo "This downloads Microsoft SDK and C++ libraries using xwin; review Microsoft's license first." >&2
    exit 2
fi

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tool_dir="$repo_dir/build/toolchains"
xwin_version="0.10.0"
xwin_sha256="d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88"
xwin_binary_sha256="0cb5d482ccf9364d0e8f3349c96ec9d781a15cfd8b0c100f364c796599190415"
xwin_archive="$tool_dir/xwin-$xwin_version.tar.gz"
xwin_url="https://github.com/Jake-Shadle/xwin/releases/download/$xwin_version/xwin-$xwin_version-x86_64-unknown-linux-musl.tar.gz"
mkdir -p "$tool_dir"

if [[ ! -x "$tool_dir/xwin" ]]; then
    curl -fL --retry 3 "$xwin_url" -o "$xwin_archive"
    printf '%s  %s\n' "$xwin_sha256" "$xwin_archive" | sha256sum -c -
    tar -xzf "$xwin_archive" -C "$tool_dir" --strip-components=1 "xwin-$xwin_version-x86_64-unknown-linux-musl/xwin"
    chmod +x "$tool_dir/xwin"
fi
printf '%s  %s\n' "$xwin_binary_sha256" "$tool_dir/xwin" | sha256sum -c -

"$tool_dir/xwin" --accept-license \
    --crt-version 14.44.17.14 --sdk-version 10.0.26100 \
    --cache-dir "$tool_dir/xwin-cache" splat \
    --output "$tool_dir/msvc" --use-winsysroot-style \
    --preserve-ms-arch-notation --include-debug-libs

ln -sfn 'msvc/Windows Kits/10/Lib/10.0.26100/um/x64' "$tool_dir/msvc-um-lib"
ln -sfn 'msvc/Windows Kits/10/Lib/10.0.26100/ucrt/x64' "$tool_dir/msvc-ucrt-lib"
echo "Windows SDK and MSVC libraries ready in $tool_dir/msvc"
