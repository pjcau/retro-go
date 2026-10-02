#!/bin/sh
set -e
cd "$(dirname "$0")"
cc -O2 -Wall -Wextra -o /tmp/line_hash_test line_hash_test.c
/tmp/line_hash_test
