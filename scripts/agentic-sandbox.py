#!/usr/bin/env python3
"""
Script to run an AI agent, or a plain shell, in a Docker sandbox.

Supported agents: Claude Code, Antigravity CLI.
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys
from typing import TypedDict

IMAGE_NAME = "cc-protocol-sandbox"

# Files baked into the image, together with the googletest tag in
# CMakeLists.txt. A change to any of them leaves an existing image stale.
# Keep in sync with the bind mounts in docker/Dockerfile, the `paths` filters
# in .github/workflows/docker.yml and the list in CONTRIBUTING.md.
IMAGE_INPUT_FILES = (
    "docker/Dockerfile",
    ".bazelversion",
    "MODULE.bazel",
    "MODULE.bazel.lock",
    "pyproject.toml",
    "uv.lock",
)

# Image label holding the hash of the inputs the image was built from.
INPUT_HASH_LABEL = "com.github.jbcoe.cc-protocol.input-hash"


class AgentCli(TypedDict):
    """Update and launch commands for an agent CLI."""

    update: str
    cmd: str


AGENT_CLIS: dict[str, AgentCli] = {
    "claude": {
        "update": "claude update",
        "cmd": "claude",
    },
    "agy": {
        "update": "curl -fsSL https://antigravity.google/cli/install.sh | bash",
        "cmd": "agy",
    },
}


def _image_input_hash(project_root: str) -> str:
    """Hash the image input files and the googletest tag in CMakeLists.txt."""
    digest = hashlib.sha256()
    for name in IMAGE_INPUT_FILES:
        with open(os.path.join(project_root, name), "rb") as file:
            digest.update(name.encode() + b"\0" + file.read() + b"\0")
    with open(os.path.join(project_root, "CMakeLists.txt"), "rb") as file:
        for tag in re.findall(rb"^\s*GIT_TAG\s+(\S+)", file.read(), re.MULTILINE):
            digest.update(tag + b"\0")
    return digest.hexdigest()


def _image_label(label: str) -> str | None:
    """Read a label from the sandbox image, or return None if there is no image."""
    result = subprocess.run(
        [
            "docker",
            "image",
            "inspect",
            "--format",
            f'{{{{index .Config.Labels "{label}"}}}}',
            IMAGE_NAME,
        ],
        capture_output=True,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else None


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


def _host_git_identity() -> tuple[str | None, str | None]:
    """Read the host's effective git user.name and user.email, if configured."""

    def _get(key: str) -> str | None:
        result = subprocess.run(
            ["git", "config", "--get", key], capture_output=True, text=True
        )
        value = result.stdout.strip()
        return value if result.returncode == 0 and value else None

    return _get("user.name"), _get("user.email")


def _identity_args() -> list[str]:
    """
    Build env args so git and jj in the sandbox use the host's git identity.

    Name and email are set independently: an unset host value is simply omitted.

    JJ environment variables are undocumented but work as intended.
    See  https://github.com/jj-vcs/jj/pull/9847.
    """
    name, email = _host_git_identity()
    args = []
    if name:
        args.extend(["-e", f"GIT_AUTHOR_NAME={name}"])
        args.extend(["-e", f"GIT_COMMITTER_NAME={name}"])
        args.extend(["-e", f"JJ_USER={name}"])
    if email:
        args.extend(["-e", f"GIT_AUTHOR_EMAIL={email}"])
        args.extend(["-e", f"GIT_COMMITTER_EMAIL={email}"])
        args.extend(["-e", f"JJ_EMAIL={email}"])
    return args


def _agent_mount_args(agent: str | None) -> list[str]:
    """Seed and mount host config so an agent CLI keeps its auth state."""
    # Use os.open with restrictive permissions in _seed_config_file to avoid
    # exposing credentials to other local users on multi-user systems.
    if agent == "agy":
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
        description="Run an AI agent (claude or agy) in a Docker "
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
        "--skip-permissions",
        action="store_true",
        help="Run claude with --dangerously-skip-permissions. The sandbox does "
        "not restrict the network and mounts the project and ~/.claude "
        "read-write.",
    )
    parser.add_argument(
        "--rebuild-docker", action="store_true", help="Rebuild the Docker image."
    )
    parser.add_argument(
        "--offline",
        action="store_true",
        help="Run the container without network access. Plain shell only.",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging."
    )
    args = parser.parse_args()

    if args.update and args.agent is None:
        parser.error("--update requires an agent")
    if args.offline and args.agent is not None:
        parser.error("--offline cannot be used with an agent")
    if args.skip_permissions and args.agent != "claude":
        parser.error("--skip-permissions requires the claude agent")

    def log(msg: str) -> None:
        if args.verbose:
            print(msg)

    project_root = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True
    ).strip()

    input_hash = _image_input_hash(project_root)
    built_from_hash = _image_label(INPUT_HASH_LABEL)
    if args.rebuild_docker or built_from_hash is None:
        log(f"--- Building Docker Sandbox: {IMAGE_NAME} ---")
        subprocess.check_call(
            [
                "docker",
                "build",
                "--target",
                "sandbox",
                "--label",
                f"{INPUT_HASH_LABEL}={input_hash}",
                "-t",
                IMAGE_NAME,
                "-f",
                os.path.join(project_root, "docker/Dockerfile"),
                project_root,
            ]
        )
    elif built_from_hash != input_hash:
        print(
            f"The {IMAGE_NAME} image is stale: its inputs have changed since it "
            "was built. Pass --rebuild-docker to refresh it."
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
        agent_cmd = cli["cmd"]
        if args.skip_permissions:
            agent_cmd += " --dangerously-skip-permissions"
        container_cmd = f"{cli['update']} && {agent_cmd}" if args.update else agent_cmd

    run_args = [
        "docker",
        "run",
        "-it",
        "--rm",
        "-v",
        f"{project_root}:/workspace",
    ]

    if args.offline:
        run_args.extend(["--network", "none"])

    run_args.extend(_agent_mount_args(args.agent))
    run_args.extend(_identity_args())

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
