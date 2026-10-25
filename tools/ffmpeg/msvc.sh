#!/usr/bin/env bash
# Normalize identification only: localized MSVC may lack the 1033 resources.
# All compilation/link arguments and exit codes pass through unchanged.
tool=$1
shift
case "$tool" in
    cl) label='Microsoft C/C++';;
    link) label='Microsoft Linker';;
    *) exit 2;;
esac
exe="$(dirname "$(command -v cl.exe)")/$tool.exe"
if [[ $# == 0 || ( $# == 1 && $1 == -nologo- ) ]]; then
    banner=$("$exe" "$@" 2>&1)
    result=$?
    printf '%s: %s\n' "$label" "$banner"
    exit "$result"
fi
exec "$exe" "$@"
