#!/bin/bash
set -e
# Find the directory containing the generated header
GEN_DIR=$(dirname $(find . -name generated_protocol_A.h | head -n 1))
python3 scripts/test_concept_errors.py --compiler c++ --source test_concept_errors.cc -- -std=c++20 -I. -I"$GEN_DIR"