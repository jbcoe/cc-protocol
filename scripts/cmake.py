#!/usr/bin/env python3
import argparse
import os
import subprocess


def main():
    parser = argparse.ArgumentParser(description="CMake helper script")
    parser.add_argument(
        "mode",
        nargs="?",
        default="test",
        choices=["build", "test", "benchmark", "b", "t", "bm"],
        help="Target mode: build (b), test (t), benchmark (bm) (default: test)",
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--debug",
        action="store_const",
        dest="preset",
        const="Debug",
        help="Use Debug preset",
    )
    group.add_argument(
        "--release",
        action="store_const",
        dest="preset",
        const="Release",
        help="Use Release preset (default)",
    )
    parser.add_argument(
        "--manual-vtable",
        action="store_true",
        help="Set XYZ_PROTOCOL_GENERATE_MANUAL_VTABLE=ON",
    )
    parser.add_argument(
        "--wrapper",
        action="store_true",
        help="Use protocol compiler wrapper (experimental)",
    )
    parser.add_argument("-B", "--build-dir", help="Build directory")
    parser.add_argument(
        "--clean", action="store_true", help="Fresh configuration and clean-first build"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging"
    )

    args, extra = parser.parse_known_args()

    # Determine preset: flag takes precedence, then default.
    preset = args.preset if args.preset else "Release"

    # Map abbreviations to full mode names
    mode_map = {
        "b": "build",
        "t": "test",
        "bm": "benchmark",
        "build": "build",
        "test": "test",
        "benchmark": "benchmark",
    }
    mode = mode_map[args.mode]

    def log(msg):
        if args.verbose:
            print(msg)

    # Configure step
    configure_args = [
        "cmake",
        "--preset",
        preset,
        f"-DXYZ_PROTOCOL_GENERATE_MANUAL_VTABLE={'ON' if args.manual_vtable else 'OFF'}",
    ]
    if args.wrapper:
        wrapper_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "protocol_compiler_wrapper.py"
        )
        configure_args.append(f"-DCMAKE_CXX_COMPILER={wrapper_path}")

    if args.build_dir:
        configure_args.extend(["-B", args.build_dir])
    if args.clean:
        configure_args.append("--fresh")

    configure_args.extend(extra)

    log(f"Running: {' '.join(configure_args)}")
    subprocess.check_call(configure_args)

    # Build step (required for build, test, benchmark)
    build_args = ["cmake", "--build"]
    if args.build_dir:
        build_args.extend([args.build_dir, "--config", preset])
    else:
        build_args.extend(["--preset", preset])
    if args.clean:
        build_args.append("--clean-first")

    log(f"Running: {' '.join(build_args)}")
    subprocess.check_call(build_args)

    # Test step
    if mode == "test":
        test_args = ["ctest"]
        if args.build_dir:
            test_args.extend(["--test-dir", args.build_dir, "-C", preset])
        else:
            test_args.extend(["--preset", preset])

        log(f"Running: {' '.join(test_args)}")
        subprocess.check_call(test_args)

    # Benchmark step
    if mode == "benchmark":
        benchmark_cmd = ["cmake", "--build"]
        if args.build_dir:
            benchmark_cmd.extend([args.build_dir, "--config", preset])
        else:
            benchmark_cmd.extend(["--preset", preset])
        benchmark_cmd.extend(["--target", "run_benchmark"])

        log(f"Running: {' '.join(benchmark_cmd)}")
        subprocess.check_call(benchmark_cmd)


if __name__ == "__main__":
    main()
