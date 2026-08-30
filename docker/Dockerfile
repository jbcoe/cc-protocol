FROM ubuntu:26.04

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
# available via Ubuntu 24.04's default repos).
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

# Create non-root user.
#
# Ubuntu 24.04+ base images ship a default `ubuntu` user at UID/GID 1000, so
# a plain `useradd vscode` would land on 1001. Host config directories that
# are bind-mounted at runtime (~/.claude, ~/.gemini, ~/.ssh, ...) are almost
# always owned by UID 1000, and VS Code's `updateRemoteUserUID` cannot remap
# `vscode` onto an ID that is already taken. Remove the stock user first so
# `vscode` gets 1000:1000 and the mounted files are readable and writable.
RUN userdel -r ubuntu \
    && useradd -m -s /bin/bash --uid 1000 vscode \
    && mkdir -p /workspace \
    && chown vscode:vscode /workspace

# Install Node.js (required for both Gemini CLI and Claude Code).
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# Switch to non-root user.
USER vscode
WORKDIR /workspace

# Configure npm for user-local global installs.
ENV NPM_CONFIG_PREFIX=/home/vscode/.npm-global
ENV PATH="/home/vscode/.npm-global/bin:/home/vscode/.local/bin:/usr/local/bin:${PATH}"
RUN mkdir -p /home/vscode/.npm-global

# Install both AI agent CLIs for compatibility with existing tooling that
# may invoke either `gemini` or `claude`.
RUN npm install -g @google/gemini-cli @anthropic-ai/claude-code

# Install Antigravity CLI (installs binary as `agy` into ~/.local/bin).
RUN curl -fsSL https://antigravity.google/cli/install.sh | bash

# Set up uv and Python environment.
RUN curl -LsSf https://astral.sh/uv/install.sh | sh
ENV UV_PROJECT_ENVIRONMENT="/home/vscode/.venv"

# Agent config dirs (~/.gemini / ~/.claude) are mounted from the host at runtime
# to persist auth state between sessions. Default config files are seeded by the launch
# script before the mount, so image-side preconfiguration is not needed here.
