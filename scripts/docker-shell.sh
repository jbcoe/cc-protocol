#!/bin/bash
set -eu -o pipefail

# Find workspace root
WORKSPACE_ROOT=$(git rev-parse --show-toplevel)
IMAGE_NAME="cc-protocol-sandbox"

REBUILD=0
for argument in "$@"; do
    if [ "$argument" == "--rebuild-docker" ]; then
        REBUILD=1
    fi
done

# Check if image exists or rebuild requested
if [ "$REBUILD" -eq 1 ] || ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "--- Building Docker Image: ${IMAGE_NAME} ---"
    docker build -t "$IMAGE_NAME" -f "${WORKSPACE_ROOT}/docker/Dockerfile" "$WORKSPACE_ROOT"
fi

echo "--- Starting Interactive Docker Shell ---"
echo "Project root ${WORKSPACE_ROOT} is mounted at /workspace"
echo ""
echo "To build and test with the C++26 reflection backend (GCC 16):"
echo "  CXX=g++-16 CC=gcc-16 ./scripts/cmake.sh --release -DXYZ_PROTOCOL_BUILD_REFLECTION_TUTORIAL=ON -B build-reflection"
echo ""

exec docker run -it --rm \
    -v "${WORKSPACE_ROOT}:/workspace" \
    -w /workspace \
    "$IMAGE_NAME" bash
