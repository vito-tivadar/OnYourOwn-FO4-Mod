#!/usr/bin/env bash
# Linux cross-build of the Windows DLL (clang-cl + lld-link with Microsoft's CRT/SDK from xwin).
# Run scripts/setup-toolchain.sh --accept-license once first.
# Output: dist/OnYourOwn.zip (Data-relative layout for mod managers).
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"

for program in xmake clang-cl lld-link llvm-lib llvm-ml llvm-rc g++ python3; do
    command -v "$program" >/dev/null || { echo "Missing tool: $program" >&2; exit 1; }
done

msvc_version="14.44.17.14"
sdk_version="10.0.26100"
sdk_dir="$repo_dir/build/toolchains/msvc"
vc_lib="$sdk_dir/VC/Tools/MSVC/$msvc_version/lib/x64"
um_lib="$repo_dir/build/toolchains/msvc-um-lib"
ucrt_lib="$repo_dir/build/toolchains/msvc-ucrt-lib"
if [[ ! -f "$vc_lib/msvcrtd.lib" || ! -f "$um_lib/uuid.lib" || ! -f "$ucrt_lib/ucrt.lib" ]]; then
    echo "Windows toolchain missing; run scripts/setup-toolchain.sh --accept-license" >&2
    exit 1
fi

# Host tests (no game or Windows toolchain needed).
logic_sources=(src/MarkerPolicy.cpp src/CompassPolicy.cpp src/MarkerClassifier.cpp src/IniSettings.cpp)
mkdir -p build/tests
g++ -std=c++23 -Wall -Wextra -Werror -Isrc src/MarkerPolicy.cpp tests/MarkerPolicyTests.cpp -o build/tests/marker_policy_tests
g++ -std=c++23 -Wall -Wextra -Werror -Isrc "${logic_sources[@]}" tests/logic_tests.cpp -o build/tests/logic_tests
g++ -std=c++23 -Wall -Wextra -Werror -Isrc "${logic_sources[@]}" tests/defaults_test.cpp -o build/tests/defaults_test
build/tests/marker_policy_tests
build/tests/logic_tests
build/tests/defaults_test

# Plugin. xmake state is kept inside build/ so nothing is written to the home directory.
export LIB="$vc_lib;$um_lib;$ucrt_lib"
export INCLUDE="$sdk_dir/Windows Kits/10/Include/$sdk_version/um;$sdk_dir/Windows Kits/10/Include/$sdk_version/shared"
export XMAKE_GLOBALDIR="$repo_dir/build/xmake-global"
export XMAKE_CONFIGDIR="$repo_dir/build/xmake-config"
mkdir -p "$XMAKE_GLOBALDIR" "$XMAKE_CONFIGDIR"
xmake f -p mingw -a x64 -m release --toolchain=oyo-msvc --runtimes=MD --ar="$repo_dir/toolchains/bin/link" -y
xmake build OnYourOwn

mkdir -p dist
python3 scripts/package.py build/mingw/x64/release/OnYourOwn.dll dist/OnYourOwn.zip --data-root Data
echo "Ready: dist/OnYourOwn.zip"
