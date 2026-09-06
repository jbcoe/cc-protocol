#!/usr/bin/env python3
"""
Script to run an AI agent, or a plain shell, in a Docker sandbox.

Supported agents: Gemini CLI, Claude Code, Antigravity CLI.
"""

import argparse
import os
import platform
import subprocess
import sys
from typing import TypedDict

IMAGE_NAME = "cc-protocol-sandbox"

# Docker named volumes persisting each tool's cache across the `--rm` sandbox
# containers, mounted at the cache paths the Dockerfile sets. Docker creates
# them on first use. Only the sandbox mounts them; the long-lived devcontainer
# keeps its cache in its own writable layer.
CACHE_VOLUMES: dict[str, str] = {"cc-protocol-uv-cache": "/home/vscode/.cache/uv"}

# Enabled by default only on macOS, where the cache volumes are known to work.
CACHE_VOLUMES_DEFAULT = platform.system() == "Darwin"


class AgentCli(TypedDict):
    """npm package and launch command for an agent CLI."""

    npm_package: str | None
    cmd: str


AGENT_CLIS: dict[str, AgentCli] = {
    "gemini": {"npm_package": "@google/gemini-cli", "cmd": "gemini"},
    "claude": {
        "npm_package": "@anthropic-ai/claude-code",
        "cmd": "claude --dangerously-skip-permissions",
    },
    "agy": {"npm_package": None, "cmd": "agy"},
}


def _seed_config_file(path: str, content: bytes) -> None:
    """Create path with content and mode 0o600, skipping silently if it exists."""
    try:
        fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        return
    try:
        os.write(fd, content)
    finally:
        os.close(fd)


def _update_command(agent: str, agent_cmd: str, npm_package: str | None) -> str:
    """Build the shell command that updates an agent CLI before running it."""
    if agent == "agy":
        return (
            "curl -fsSL https://antigravity.google/cli/install.sh | bash && "
            f"{agent_cmd}"
        )
    return (
        "export NPM_CONFIG_PREFIX=~/.npm-global && "
        "export PATH=~/.npm-global/bin:$PATH && "
        f"npm install -g {npm_package}@latest && {agent_cmd}"
    )


def _agent_mount_args(agent: str | None) -> list[str]:
    """Seed and mount host config so an agent CLI keeps its auth state."""
    # Use os.open with restrictive permissions in _seed_config_file to avoid
    # exposing credentials to other local users on multi-user systems.
    if agent in ("gemini", "agy"):
        gemini_config_dir = os.path.expanduser("~/.gemini")
        os.makedirs(gemini_config_dir, mode=0o700, exist_ok=True)
        _seed_config_file(
            os.path.join(gemini_config_dir, "trustedFolders.json"),
            b'{"/workspace": "TRUST_FOLDER"}',
        )
        _seed_config_file(
            os.path.join(gemini_config_dir, "settings.json"),
            b'{"selectedAuthType": "oauth-personal"}',
        )
        return ["-v", f"{gemini_config_dir}:/home/vscode/.gemini"]

    if agent == "claude":
        host_claude_dir = os.path.expanduser("~/.claude")
        host_claude_json = os.path.expanduser("~/.claude.json")
        os.makedirs(host_claude_dir, mode=0o700, exist_ok=True)
        # Ensure the file exists on the host so Docker doesn't create it as a
        # directory.
        if os.path.exists(host_claude_json):
            if not os.path.isfile(host_claude_json):
                sys.exit(
                    f"Expected {host_claude_json} to be a regular file, but found "
                    "a different filesystem object. Remove or rename it and rerun."
                )
        else:
            _seed_config_file(host_claude_json, b"")
        return [
            "-v",
            f"{host_claude_dir}:/home/vscode/.claude",
            "-v",
            f"{host_claude_json}:/home/vscode/.claude.json",
        ]

    return []


def main() -> None:
    """Provide the main entry point for the agentic sandbox script."""
    parser = argparse.ArgumentParser(
        description="Run an AI agent (gemini, claude, or agy) in a Docker "
        "sandbox, or a plain shell if no agent is given."
    )
    parser.add_argument(
        "agent",
        nargs="?",
        choices=list(AGENT_CLIS),
        help="AI agent to run inside the sandbox. Omit for a plain shell.",
    )
    parser.add_argument(
        "--update",
        action="store_true",
        help="Update the agent CLI inside the container before running. "
        "Requires an agent.",
    )
    parser.add_argument(
        "--rebuild-docker", action="store_true", help="Rebuild the Docker image."
    )
    parser.add_argument(
        "--cache-volumes",
        action=argparse.BooleanOptionalAction,
        default=CACHE_VOLUMES_DEFAULT,
        help="Mount the persistent uv cache volumes.",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging."
    )
    args = parser.parse_args()

    if args.update and args.agent is None:
        parser.error("--update requires an agent")

    def log(msg: str) -> None:
        if args.verbose:
            print(msg)

    project_root = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True
    ).strip()

    image_exists = (
        subprocess.run(
            ["docker", "image", "inspect", IMAGE_NAME], capture_output=True
        ).returncode
        == 0
    )
    if args.rebuild_docker or not image_exists:
        log(f"--- Building Docker Sandbox: {IMAGE_NAME} ---")
        subprocess.check_call(
            [
                "docker",
                "build",
                "--target",
                "sandbox",
                "-t",
                IMAGE_NAME,
                "-f",
                os.path.join(project_root, "docker/Dockerfile"),
                project_root,
            ]
        )

    session_name = args.agent.capitalize() if args.agent else "Shell"
    log(f"--- Starting Sandboxed {session_name} Session ---")
    log(f"Note: Your current directory {project_root} is mounted to /workspace")

    if args.agent is None:
        print("To build and test (GCC trunk with C++26 reflection):")
        print("  ./scripts/bazel.sh")
        container_cmd = None
    else:
        cli = AGENT_CLIS[args.agent]
        container_cmd = (
            _update_command(args.agent, cli["cmd"], cli["npm_package"])
            if args.update
            else cli["cmd"]
        )

    cache_mounts = []
    if args.cache_volumes:
        for volume, target in CACHE_VOLUMES.items():
            cache_mounts.extend(["-v", f"{volume}:{target}"])

    run_args = [
        "docker",
        "run",
        "-it",
        "--rm",
        "-v",
        f"{project_root}:/workspace",
    ]

    run_args.extend(cache_mounts)
    run_args.extend(_agent_mount_args(args.agent))

    if "TERM" in os.environ:
        run_args.extend(["-e", f"TERM={os.environ['TERM']}"])
    if "COLORTERM" in os.environ:
        run_args.extend(["-e", f"COLORTERM={os.environ['COLORTERM']}"])

    if container_cmd is None:
        run_args.extend([IMAGE_NAME, "bash"])
    else:
        run_args.extend([IMAGE_NAME, "bash", "-c", container_cmd])

    try:
        subprocess.run(run_args, check=True)
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode)


if __name__ == "__main__":
    main()
