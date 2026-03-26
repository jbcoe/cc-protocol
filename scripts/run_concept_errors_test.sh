#!/bin/bash
set -euo pipefail

# Find the directory containing the generated header
HEADER_FILE=$(find . -path "*/generated/protocol_A.h" | head -n 1)
if [ -z "$HEADER_FILE" ]; then
    echo "Error: generated/protocol_A.h not found" >&2
    exit 1
fi
GEN_DIR=$(dirname "$HEADER_FILE")

uv run pytest scripts/test_concept_errors.py --compiler=c++ --flags="-std=c++20" --flags="-I$(dirname "$GEN_DIR")" --flags="-I."
