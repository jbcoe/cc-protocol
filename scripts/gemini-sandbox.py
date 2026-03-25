#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="Run Gemini CLI in a Docker sandbox.")
    parser.add_argument(
        "--update-gemini",
        action="store_true",
        help="Update Gemini CLI inside the container before running.",
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

    if "GEMINI_API_KEY" not in os.environ:
        print("Error: GEMINI_API_KEY environment variable is not set.", file=sys.stderr)
        print("Please set it before running this script:", file=sys.stderr)
        print("  export GEMINI_API_KEY='your_api_key_here'", file=sys.stderr)
        sys.exit(1)

    project_root = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True
    ).strip()
    image_name = "cc-protocol-gemini-sandbox"
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

    log("--- Starting Sandboxed Gemini Session ---")
    log(f"Note: Your current directory {project_root} is mounted to /workspace")

    if args.update_gemini:
        container_cmd = "npm install -g @google/gemini-cli@latest --silent && gemini"
    else:
        container_cmd = "gemini"

    run_args = [
        "docker",
        "run",
        "-it",
        "--rm",
        "-v",
        f"{project_root}:/workspace",
        "-e",
        f"GEMINI_API_KEY={os.environ['GEMINI_API_KEY']}",
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
