#!/usr/bin/env bash

set -e
set -u
set -x

./scripts/source_files.sh | xargs -t clang-format-14 --dry-run --Werror
