#!/bin/sh
set -e
cd "$(dirname "$0")"
cc -O2 -Wall -Wextra -o /tmp/scale_line_test scale_line_test.c
/tmp/scale_line_test
