#!/bin/bash
set -euo pipefail

# Find the directory containing the generated header
HEADER_FILE=$(find . -name "generated/protocol_A.h" | head -n 1)
if [ -z "$HEADER_FILE" ]; then
    echo "Error: generated/protocol_A.h not found" >&2
    exit 1
fi
GEN_DIR=$(dirname "$HEADER_FILE")

python3 scripts/test_concept_errors.py --compiler c++ --source test_concept_errors.cc -- -std=c++20 -I"$(dirname "$GEN_DIR")" -I.
