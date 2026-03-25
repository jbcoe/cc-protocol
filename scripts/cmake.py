#!/usr/bin/env python3
import argparse
import subprocess


def main():
    parser = argparse.ArgumentParser(description="CMake helper script")
    parser.add_argument(
        "--manual-vtable",
        action="store_true",
        help="Set XYZ_PROTOCOL_GENERATE_MANUAL_VTABLE=ON",
    )
    parser.add_argument("-B", "--build-dir", help="Build directory")
    parser.add_argument(
        "--clean", action="store_true", help="Fresh configuration and clean-first build"
    )
    parser.add_argument(
        "preset", nargs="?", default="Release", help="CMake preset (default: Release)"
    )

    args = parser.parse_args()

    # Configure step
    configure_args = [
        "cmake",
        "--preset",
        args.preset,
        f"-DXYZ_PROTOCOL_GENERATE_MANUAL_VTABLE={'ON' if args.manual_vtable else 'OFF'}",
    ]
    if args.build_dir:
        configure_args.extend(["-B", args.build_dir])
    if args.clean:
        configure_args.append("--fresh")

    print(f"Running: {' '.join(configure_args)}")
    subprocess.check_call(configure_args)

    # Build step
    build_args = ["cmake", "--build"]
    if args.build_dir:
        build_args.extend([args.build_dir, "--config", args.preset])
    else:
        build_args.extend(["--preset", args.preset])
    if args.clean:
        build_args.append("--clean-first")

    print(f"Running: {' '.join(build_args)}")
    subprocess.check_call(build_args)

    # Test step
    test_args = ["ctest"]
    if args.build_dir:
        test_args.extend(["--test-dir", args.build_dir, "-C", args.preset])
    else:
        test_args.extend(["--preset", args.preset])

    print(f"Running: {' '.join(test_args)}")
    subprocess.check_call(test_args)


if __name__ == "__main__":
    main()
