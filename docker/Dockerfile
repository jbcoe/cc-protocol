FROM ubuntu:24.04

# Install base tools and C++ development tools.
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    curl \
    g++ \
    gdb \
    git \
    gpg \
    lcov \
    llvm \
    ninja-build \
    python-is-python3 \
    python3-pip \
    python3-venv \
    software-properties-common \
    ssh \
    unzip \
    wget \
&& rm -rf /var/lib/apt/lists/*

# Install newer CMake from kitware.
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null \
&& apt-add-repository -y 'deb https://apt.kitware.com/ubuntu/ noble main' \
&& apt-get update && apt-get install -y --no-install-recommends cmake \
&& rm -rf /var/lib/apt/lists/*

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
RUN useradd -m -s /bin/bash vscode \
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
