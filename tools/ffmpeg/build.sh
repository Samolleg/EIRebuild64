#!/usr/bin/env bash
# Run in MSYS2 bash inheriting vcvars64.bat. Never invokes a media player/test.
set -euo pipefail
die() { printf '%s\n' "$*" >&2; exit 1; }

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
vendor="$repo/third_party/ffmpeg"
source "$vendor/source.lock"
[[ $# == 1 ]] || die 'Usage: bash tools/ffmpeg/build.sh /c/ffmpeg-build/X64P19 (fresh path without spaces)'

# Missing prerequisites must not trigger package installation or a reduced build.
missing=()
for tool in make nasm cl.exe lib.exe dumpbin.exe tar xz curl sha256sum sed diff cmp cp mv grep cygpath; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
[[ ${#missing[@]} == 0 ]] || die "Missing build tools: ${missing[*]}. See tools/ffmpeg/README.md."
[[ ${VSCMD_ARG_TGT_ARCH:-} == x64 ]] || die 'Run vcvars64.bat first; MSVC target must be x64.'

work_root=$(cygpath -am "$1")
[[ "$work_root" != *[[:space:]]* ]] || die 'FFmpeg configure/make requires a work path without whitespace.'
work_root=$(cygpath -u "$work_root")
output="$repo/dependencies/ffmpeg/$version/x64"
[[ ! -e "$output" ]] || die "Output already exists: $output. Preserve it and select a separate revision before rebuilding."
for dir in source build install; do
    [[ ! -e "$work_root/$dir" ]] || die "Existing work: $work_root/$dir. Use a fresh work root."
done

downloads="$repo/dependencies/ffmpeg/$version/downloads"
mkdir -p -- "$downloads"
if [[ ! -f "$downloads/$archive" ]]; then
    # A failed download is never left under the final archive name.
    curl --fail --location --output "$downloads/$archive.part" "$url"
    mv -- "$downloads/$archive.part" "$downloads/$archive"
fi
printf '%s  %s\n' "$sha256" "$downloads/$archive" | sha256sum --check --status || die 'Source archive SHA-256 differs from source.lock.'

mkdir -p -- "$work_root/source" "$work_root/build"
tar -xJf "$downloads/$archive" -C "$work_root/source"
source_dir="$work_root/source/ffmpeg-$version"
args=()
while IFS= read -r arg || [[ -n "$arg" ]]; do
    arg=${arg%$'\r'}
    [[ -z "$arg" || "$arg" == \#* ]] || args+=("$arg")
done < "$vendor/configure.args"

cd -- "$work_root/build"
# Do not inherit compiler/linker options from another project.
unset CL _CL_ LINK _LINK_
"$source_dir/configure" "${args[@]}" --prefix="$work_root/install"
# These are configure-result guards, not test/decoding runs.
grep -qx '#define CONFIG_GPL 0' config.h || die 'GPL enabled unexpectedly.'
grep -qx '#define CONFIG_NONFREE 0' config.h || die 'Nonfree enabled unexpectedly.'
grep -qx '#define CONFIG_VERSION3 0' config.h || die 'Version3 enabled unexpectedly.'
make -j"${NUMBER_OF_PROCESSORS:-2}"
make install

# Install locally without exposing workspace spaces to generated Makefiles.
# Copy upstream license/source identity and the actual configure receipt too.
notice="$work_root/install/share/ffmpeg"
mkdir -p -- "$notice"
cp -- "$vendor/COPYING.LGPLv2.1" "$vendor/LICENSE.md" "$vendor/PROVENANCE.md" \
    "$vendor/COMPONENTS.md" "$vendor/source.lock" "$vendor/configure.args" "$notice/"
cp -- config.h config_components.h ffbuild/config.mak ffbuild/config.log "$notice/"
for dir in bin include lib; do
    [[ -d "$work_root/install/$dir" ]] || die "Missing installed directory: $dir"
done
mkdir -p -- "$(dirname -- "$output")"
cp -R -- "$work_root/install" "$output"
printf 'FFmpeg %s shared x64 installed locally: %s\n' "$version" "$output"
