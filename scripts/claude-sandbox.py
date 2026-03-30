#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Run Claude Code CLI in a Docker sandbox."
    )
    parser.add_argument(
        "--update-claude",
        action="store_true",
        help="Update Claude Code CLI inside the container before running.",
    )
    parser.add_argument(
        "--rebuild-docker", action="store_true", help="Rebuild the Docker image."
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging."
    )

    args = parser.parse_args()

    def log(msg):
        if args.verbose:
            print(msg)

    if "ANTHROPIC_API_KEY" not in os.environ:
        print(
            "Error: ANTHROPIC_API_KEY environment variable is not set.", file=sys.stderr
        )
        print("Please set it before running this script:", file=sys.stderr)
        print("  export ANTHROPIC_API_KEY='your_api_key_here'", file=sys.stderr)
        sys.exit(1)

    project_root = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True
    ).strip()
    image_name = "cc-protocol-claude-sandbox"
    dockerfile = "docker/Dockerfile"

    if args.rebuild_docker:
        log(f"--- Building Docker Sandbox: {image_name} ---")
        subprocess.check_call(
            [
                "docker",
                "build",
                "-t",
                image_name,
                "-f",
                os.path.join(project_root, dockerfile),
                project_root,
            ]
        )

    log("--- Starting Sandboxed Claude Session ---")
    log(f"Note: Your current directory {project_root} is mounted to /workspace")

    if args.update_claude:
        container_cmd = "npm install -g @anthropic-ai/claude-code@latest --silent && claude --dangerously-skip-permissions"
    else:
        container_cmd = "claude --dangerously-skip-permissions"

    run_args = [
        "docker",
        "run",
        "-it",
        "--rm",
        "-v",
        f"{project_root}:/workspace",
        "-e",
        f"ANTHROPIC_API_KEY={os.environ['ANTHROPIC_API_KEY']}",
    ]

    if "TERM" in os.environ:
        run_args.extend(["-e", f"TERM={os.environ['TERM']}"])
    if "COLORTERM" in os.environ:
        run_args.extend(["-e", f"COLORTERM={os.environ['COLORTERM']}"])

    run_args.extend([image_name, "bash", "-c", container_cmd])

    try:
        subprocess.run(run_args, check=True)
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode)


if __name__ == "__main__":
    main()
