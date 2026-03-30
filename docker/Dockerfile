# Dockerfile for Ubuntu 24.04 with C++ development tools.
FROM ubuntu:24.04

# Install gcc, clang and some supporting tools for downloading/installing later tools.
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

# Create non-root user
RUN useradd -m -s /bin/bash vscode \
    && mkdir -p /workspace \
    && chown vscode:vscode /workspace

# Install Node.js (required for Gemini CLI)
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# Set up npm to install globally in vscode's home directory
ENV NPM_CONFIG_PREFIX=/home/vscode/.npm-global
ENV PATH="/home/vscode/.npm-global/bin:${PATH}"

# Switch to non-root user
USER vscode
WORKDIR /workspace

# Install Gemini CLI and Claude Code CLI
RUN mkdir -p /home/vscode/.npm-global \
    && npm install -g @google/gemini-cli \
    && npm install -g @anthropic-ai/claude-code

# Set up uv and Python environment
RUN curl -LsSf https://astral.sh/uv/install.sh | sh

# Set up environment variables
ENV PATH="/home/vscode/.local/bin:/usr/local/bin:${PATH}"
ENV UV_PROJECT_ENVIRONMENT="/home/vscode/.venv"

# Pre-configure Gemini CLI
RUN mkdir -p /home/vscode/.gemini \
    && echo '{"/workspace": "TRUST_FOLDER"}' > /home/vscode/.gemini/trustedFolders.json \
    && echo '{"security": {"auth": {"selectedType": "gemini-api-key"}}}' > /home/vscode/.gemini/settings.json

# Pre-configure Claude Code CLI
RUN mkdir -p /home/vscode/.claude \
    && echo '{"permissions": {"allow": ["Bash(*)", "Read(*)", "Write(*)", "Edit(*)", "Glob(*)", "Grep(*)"]}}' > /home/vscode/.claude/settings.json
