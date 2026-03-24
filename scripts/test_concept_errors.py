import argparse
import subprocess
import sys


def check_compile_error(compiler, flags, source, define, expected_terms):
    cmd = [compiler] + flags + [f"-D{define}", "-c", source]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode == 0:
        print(f"FAIL: {define} compiled successfully but should have failed.")
        return False

    output = res.stderr + res.stdout
    for term in expected_terms:
        if term not in output:
            print(f"FAIL: {define} missing expected term '{term}' in output.")
            print("--- COMPILER OUTPUT ---")
            print(output)
            print("-----------------------")
            return False

    print(
        f"PASS: {define} correctly emitted concept errors containing {expected_terms}"
    )
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("flags", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    # We want to remove the '--' if it's there
    flags = [f for f in args.flags if f != "--"]

    tests = [
        ("TEST_MISSING_METHOD", ["xyz_protocol_concept_A", "name()"]),
        ("TEST_WRONG_RETURN_TYPE", ["xyz_protocol_concept_A", "count()"]),
        ("TEST_MISSING_CONST", ["xyz_protocol_concept_A", "name()"]),
    ]

    success = True
    for define, terms in tests:
        if not check_compile_error(args.compiler, flags, args.source, define, terms):
            success = False

    if not success:
        sys.exit(1)


if __name__ == "__main__":
    main()
