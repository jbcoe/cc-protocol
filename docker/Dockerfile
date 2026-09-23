FROM ubuntu:26.04 AS base

# Fail a RUN step when any command in a pipeline fails, not only the last.
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

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
    jq \
    lcov \
    llvm \
    ninja-build \
    python-is-python3 \
    python3-pip \
    python3-venv \
    ripgrep \
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
# The snapshot is pinned to the one CI installs; the reason is with the
# package-url default in .github/actions/install-gcc-trunk/action.yml.
RUN ARCH=$(dpkg --print-architecture) && \
    case "$ARCH" in \
        amd64) \
            wget -nv https://kayari.org/gcc-latest/gcc-latest_17.0.0-20260906gitb6dabcdc335a.deb -O /tmp/gcc-latest.deb \
            && apt-get update && apt-get install -y --no-install-recommends /tmp/gcc-latest.deb \
            && rm -f /tmp/gcc-latest.deb \
            && rm -rf /var/lib/apt/lists/* \
            ;; \
        *) echo "gcc-latest is x86_64-only; the reflection build will fall back to gcc-16 on $ARCH" ;; \
    esac

# Install bazelisk.
RUN ARCH=$(dpkg --print-architecture) && \
    wget -nv "https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-${ARCH}" -O /usr/local/bin/bazelisk \
    && chmod +x /usr/local/bin/bazelisk \
    && ln -s /usr/local/bin/bazelisk /usr/local/bin/bazel

# Install the GitHub CLI (gh) from its own apt repository, per
# https://github.com/cli/cli/blob/trunk/docs/install_linux.md (not reliably
# available via Ubuntu 26.04's default repos).
RUN mkdir -p /etc/apt/keyrings \
    && chmod 755 /etc/apt/keyrings \
    && wget -nv -O /etc/apt/keyrings/githubcli-archive-keyring.gpg https://cli.github.com/packages/githubcli-archive-keyring.gpg \
    && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | tee /etc/apt/sources.list.d/github-cli.list >/dev/null \
    && apt-get update && apt-get install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/*

# Install jj (Jujutsu VCS) from its GitHub release binary. The jujutsu apt
# package isn't built for Ubuntu 26.04 (resolute) or Debian stable; it only
# reaches Debian testing/unstable and Ubuntu's current development release.
RUN ARCH=$(dpkg --print-architecture) && \
    case "$ARCH" in \
        amd64) JJ_ARCH=x86_64 ;; \
        arm64) JJ_ARCH=aarch64 ;; \
        *) echo "Unsupported architecture for jj: $ARCH" >&2; exit 1 ;; \
    esac && \
    wget -nv "https://github.com/jj-vcs/jj/releases/download/v0.43.0/jj-v0.43.0-${JJ_ARCH}-unknown-linux-musl.tar.gz" -O /tmp/jj.tar.gz \
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

# Switch to non-root user.
USER vscode
WORKDIR /workspace

ENV PATH="/home/vscode/.local/bin:/usr/local/bin:${PATH}"

# Install Claude Code CLI with the native installer (installs `claude` into
# ~/.local/bin). It needs no Node.js and updates itself in the background.
RUN curl -fsSL https://claude.ai/install.sh | bash

# Install Antigravity CLI (installs binary as `agy` into ~/.local/bin).
RUN curl -fsSL https://antigravity.google/cli/install.sh | bash

# Set up uv and Python environment.
RUN curl -LsSf https://astral.sh/uv/install.sh | sh
ENV UV_PROJECT_ENVIRONMENT="/home/vscode/.venv"
ENV UV_CACHE_DIR="/home/vscode/.cache/uv"

# Agent config dirs (~/.gemini / ~/.claude) are mounted from the host at runtime
# to persist auth state between sessions. Default config files are seeded by the launch
# script before the mount, so image-side preconfiguration is not needed here.

# Avoid dubious ownership issues flagged by recent versions of git by trusting `/workspace`.
# https://git-scm.com/docs/git-config#Documentation/git-config.txt-safedirectory
RUN git config --global --add safe.directory /workspace

# Pre-warm bazelisk's download and install of the pinned Bazel binary to avoid
# fetching and installing it on every run. This is in `base`, not `sandbox`,
# so the devcontainer (which builds only `base`) gets it too.
# https://github.com/bazelbuild/bazelisk#how-does-bazelisk-know-which-bazel-version-to-run
RUN --mount=type=bind,source=.bazelversion,target=.bazelversion \
    USE_BAZEL_VERSION="$(cat .bazelversion)" bazel version

# The sandbox image needs additional setup to pre-warm caches used at runtime.
FROM base AS sandbox

# Pre-warm uv's package cache and build the venv from the pinned lockfile so
# `uv run`/`uv sync` inside the sandbox need no network.
RUN --mount=type=bind,source=pyproject.toml,target=pyproject.toml \
    --mount=type=bind,source=uv.lock,target=uv.lock \
    uv sync --locked

# Clone googletest into the image so CMake builds need no network. With
# FETCHCONTENT_SOURCE_DIR_GOOGLETEST set, FetchContent performs no download or
# update step and each build tree still compiles its own copy. The tag is read
# from CMakeLists.txt's GIT_TAG so the two can't drift out of sync. Only the
# tag on the first GIT_TAG line is taken, so a trailing comment or a CRLF line
# ending never reaches git.
ENV FETCHCONTENT_SOURCE_DIR_GOOGLETEST="/home/vscode/.cache/googletest-src"
RUN --mount=type=bind,source=CMakeLists.txt,target=CMakeLists.txt \
    git clone --depth 1 \
        --branch "$(sed -n '/^\s*GIT_TAG\s\+\([^[:space:]#]\+\).*/{s//\1/p;q}' CMakeLists.txt)" \
        https://github.com/google/googletest.git \
        "$FETCHCONTENT_SOURCE_DIR_GOOGLETEST"

# Pre-warm Bazel's repository cache from the target graph so sandbox builds
# need no network for external dependencies. Only the files that define the
# graph are mounted; the fetch needs no sources. It is last in the stage, but
# a miss in an earlier layer still re-runs it: the googletest clone above is
# keyed on the whole of CMakeLists.txt, so any CMake edit re-clones googletest
# and re-runs this fetch. --lockfile_mode=error fails the build on a stale
# MODULE.bazel.lock instead of resolving online.
#
# Most of the layer is the repository cache, under
# ~/.cache/bazel/_bazel_vscode/cache. The layer also captures Bazel's output
# base for /workspace, which the runtime container reuses because the checkout
# mounts at the same path.
#
# The files mounted in this stage and .bazelversion above are the image's
# inputs; keep them in sync with IMAGE_INPUT_FILES in
# scripts/agentic-sandbox.py.
RUN --mount=type=bind,source=.bazelrc,target=.bazelrc \
    --mount=type=bind,source=.bazelversion,target=.bazelversion \
    --mount=type=bind,source=BUILD.bazel,target=BUILD.bazel \
    --mount=type=bind,source=MODULE.bazel,target=MODULE.bazel \
    --mount=type=bind,source=MODULE.bazel.lock,target=MODULE.bazel.lock \
    bazel fetch --lockfile_mode=error //...
