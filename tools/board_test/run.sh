#!/bin/sh
# tools/extract_board_data.py: a made-up board, then the real one when it
# is there (HH27_HW, or hackerhotel2027-hardware next to this repository;
# CI fetches it). Without the real board that part prints SKIP.
set -e
python3 "$(dirname "$0")/board_test.py"
