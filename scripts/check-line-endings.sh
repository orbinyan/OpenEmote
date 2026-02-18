#!/bin/bash

# SPDX-FileCopyrightText: 2025 Rasmus Karlsson <rasmus.karlsson@pajlada.com>
#
# SPDX-License-Identifier: CC0-1.0

set -eu

fail="0"

if command -v dos2unix >/dev/null 2>&1; then
    dos2unix --version

    while read -r file; do
        num_dos_line_endings=$(dos2unix -id "$file" | awk '/[0-9]+/{print $(NF-1)}')
        if [ "$num_dos_line_endings" -gt "0" ]; then
            >&2 echo "File '$file' contains $num_dos_line_endings DOS line-endings, it should only be using unix line-endings!"
            fail="1"
        fi
    done < <(find src/ -type f \( -iname "*.hpp" -o -iname "*.cpp" \))
else
    >&2 echo "dos2unix not found; using grep fallback for CRLF detection"

    while read -r file; do
        num_dos_line_endings=$(grep -U -c $'\r$' "$file" 2>/dev/null || true)
        if [ "${num_dos_line_endings:-0}" -gt "0" ]; then
            >&2 echo "File '$file' contains $num_dos_line_endings DOS line-endings, it should only be using unix line-endings!"
            fail="1"
        fi
    done < <(find src/ -type f \( -iname "*.hpp" -o -iname "*.cpp" \))
fi

if [ "$fail" = "1" ]; then
    >&2 echo "At least one file is not using unix line-endings - check the output above"
    exit 1
fi

>&2 echo "Every file seems to be using unix line-endings. Good job!"
