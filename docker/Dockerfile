# Two build stages sharing a common base:
#   base    - C++ toolchain and language tools; the devcontainer builds this.
#   sandbox - base plus the agent CLIs and a persistent uv cache; built by
#             scripts/agentic-sandbox.py for ephemeral `--rm` agent containers.
# The devcontainer container is long-lived, so it keeps its uv cache in its own
# writable layer and mounts no cache volume; only the sandbox does.
FROM ubuntu:26.04 AS base

# Install base tools and C++ development tools.
#
# clang-tidy is pinned to the same major version as the "Clang Tidy" CI
# workflow (COMPILER_VERSION in .github/workflows/clang-tidy.yml) so that a
# local `./scripts/cmake.sh --clang-tidy` run reports the same findings as CI.
# The unversioned `clang-tidy` package on this Ubuntu release is LLVM 21.
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-tidy-22 \
    cmake \
    curl \
    g++ \
    gcc-16 \
    g++-16 \
    gdb \
    git \
    gpg \
    lcov \
    llvm \
    ninja-build \
    python-is-python3 \
    python3-pip \
    python3-venv \
    ssh \
    unzip \
    wget \
    && rm -rf /var/lib/apt/lists/* \
    && ln -s /usr/bin/clang-tidy-22 /usr/local/bin/clang-tidy

# Install a GCC trunk snapshot for the C++26 reflection backend, from
# https://jwakely.github.io/pkg-gcc-latest/. Ubuntu's gcc-16 package (installed
# above) is a frozen experimental snapshot whose reflection support no longer
# receives fixes (issue #122), so we prefer this continuously-updated trunk
# build when available. It is x86_64 only, so other architectures fall back to
# gcc-16. It installs to /opt/gcc-latest; scripts/cmake.py locates it there
# explicitly, and CMakeLists.txt adds a build rpath so linked executables can
# find its libstdc++ at runtime without any PATH or LD_LIBRARY_PATH changes.
RUN ARCH=$(dpkg --print-architecture) && \
    case "$ARCH" in \
        amd64) \
            wget -nv https://kayari.org/gcc-latest/gcc-latest.deb -O /tmp/gcc-latest.deb \
            && apt-get update && apt-get install -y --no-install-recommends /tmp/gcc-latest.deb \
            && rm -f /tmp/gcc-latest.deb \
            && rm -rf /var/lib/apt/lists/* \
            ;; \
        *) echo "gcc-latest is x86_64-only; the reflection build will fall back to gcc-16 on $ARCH" ;; \
    esac

# Install bazelisk.
RUN ARCH=$(dpkg --print-architecture) && \
    wget https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-${ARCH} -O /usr/local/bin/bazelisk \
    && chmod +x /usr/local/bin/bazelisk \
    && ln -s /usr/local/bin/bazelisk /usr/local/bin/bazel

# Install the GitHub CLI (gh) from its own apt repository, per
# https://github.com/cli/cli/blob/trunk/docs/install_linux.md (not reliably
# available via Ubuntu's default repos).
RUN mkdir -p -m 755 /etc/apt/keyrings \
    && wget -nv -O /etc/apt/keyrings/githubcli-archive-keyring.gpg https://cli.github.com/packages/githubcli-archive-keyring.gpg \
    && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | tee /etc/apt/sources.list.d/github-cli.list >/dev/null \
    && apt-get update && apt-get install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/*

# Install jj (Jujutsu VCS) from its GitHub release binary. The jujutsu apt
# package isn't built for Ubuntu 24.04 (noble) or Debian stable; it only
# reaches Debian testing/unstable and Ubuntu's current development release.
RUN ARCH=$(dpkg --print-architecture) && \
    case "$ARCH" in \
        amd64) JJ_ARCH=x86_64 ;; \
        arm64) JJ_ARCH=aarch64 ;; \
        *) echo "Unsupported architecture for jj: $ARCH" >&2; exit 1 ;; \
    esac && \
    wget https://github.com/jj-vcs/jj/releases/download/v0.43.0/jj-v0.43.0-${JJ_ARCH}-unknown-linux-musl.tar.gz -O /tmp/jj.tar.gz \
    && tar -xzf /tmp/jj.tar.gz -C /usr/local/bin ./jj \
    && rm /tmp/jj.tar.gz

# Install Node.js. The devcontainer's Claude Code VS Code extension needs it,
# and the sandbox stage installs the agent CLIs on top of it.
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# Create the non-root user `vscode` at UID 1000. Ubuntu ships a stock `ubuntu`
# user at UID 1000; remove it first so `vscode` gets 1000.
RUN userdel -r ubuntu \
    && useradd -m -s /bin/bash --uid 1000 vscode \
    && mkdir -p /workspace \
    && chown vscode:vscode /workspace

USER vscode
WORKDIR /workspace

ENV PATH="/home/vscode/.local/bin:/usr/local/bin:${PATH}"

# Set up uv and the Python environment. UV_CACHE_DIR names the download cache;
# in the devcontainer it lives in the container's writable layer, and the
# sandbox stage mounts a persistent volume there instead.
RUN curl -LsSf https://astral.sh/uv/install.sh | sh
ENV UV_PROJECT_ENVIRONMENT="/home/vscode/.venv"
ENV UV_CACHE_DIR="/home/vscode/.cache/uv"
ENV UV_LINK_MODE="copy"
RUN mkdir -p /home/vscode/.cache/uv

# Pre-warm bazelisk's download of the pinned Bazel binary (~60MB) so the
# ephemeral sandbox reuses the baked copy instead of fetching it on every run.
# bazelisk reads the version from .bazelversion; the runtime bind mount supplies
# the same file, so the cached binary matches and is reused. Runs as vscode so
# the cache lands in that user's ~/.cache/bazelisk.
COPY .bazelversion /workspace/.bazelversion
RUN bazel version


FROM base AS sandbox

# Configure npm for user-local global installs, then install the agent CLIs.
ENV NPM_CONFIG_PREFIX=/home/vscode/.npm-global
ENV PATH="/home/vscode/.npm-global/bin:${PATH}"
RUN mkdir -p /home/vscode/.npm-global
RUN npm install -g @google/gemini-cli @anthropic-ai/claude-code

# Install Antigravity CLI (installs binary as `agy` into ~/.local/bin).
RUN curl -fsSL https://antigravity.google/cli/install.sh | bash

# Agent config dirs (~/.gemini / ~/.claude) are mounted from the host at runtime
# to persist auth state between sessions. Default config files are seeded by the launch
# script before the mount, so image-side preconfiguration is not needed here.
