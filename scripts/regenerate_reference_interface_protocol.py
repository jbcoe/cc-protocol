#!/usr/bin/env python3
"""Script to regenerate reference_interface_protocol.h from reference_interface.h."""

import os
import subprocess
import sys


def main() -> None:
    """Regenerate reference_interface_protocol.h from reference_interface.h."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace_root = os.path.dirname(script_dir)
    os.chdir(workspace_root)

    compiler = sys.argv[1] if len(sys.argv) > 1 else "c++"

    print(f"Regenerating reference_interface_protocol.h using compiler '{compiler}'...")

    cmd = [
        sys.executable,
        "scripts/generate_protocol.py",
        "reference_interface.h",
        "reference_interface_protocol.h",
        "--class_name",
        "ReferenceInterface",
        "--template",
        "scripts/protocol.j2",
        "--compiler",
        compiler,
        "--header",
        "reference_interface.h",
    ]

    try:
        subprocess.run(cmd, check=True)
        print("Regeneration complete!")
    except subprocess.CalledProcessError as e:
        print(
            f"Error: Code generation failed with exit code {e.returncode}",
            file=sys.stderr,
        )
        sys.exit(e.returncode)


if __name__ == "__main__":
    main()
