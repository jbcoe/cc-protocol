#!/bin/bash

set -eu -o pipefail

# Get the workspace root
WORKSPACE_ROOT=$(git rev-parse --show-toplevel)

# Consume our own `--uv-debug` flag (verbose uv logging, off by default) before
# forwarding the remaining arguments to the python script.
uv_verbosity=()
script_args=()
for arg in "$@"; do
  if [[ "$arg" == "--uv-debug" ]]; then
    uv_verbosity=(--verbose)
  else
    script_args+=("$arg")
  fi
done

# Change directory to the workspace root and run the python script
cd "$WORKSPACE_ROOT"
uv run "${uv_verbosity[@]}" scripts/bazel.py "${script_args[@]}"
