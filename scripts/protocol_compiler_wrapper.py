#!/usr/bin/env python3
import os
import re
import subprocess
import sys
import tempfile

import clang.cindex

from scripts.generate_protocol import generate_protocol_from_tu
from scripts.generate_protocol import get_compiler_args


def main():
    compiler = os.environ.get("CXX", "g++")
    args = sys.argv[1:]

    if "-c" not in args and "--compile" not in args:
        subprocess.run([compiler] + args)
        return

    source_files = [a for a in args if a.endswith((".cc", ".cpp", ".cxx", ".C"))]
    if not source_files:
        subprocess.run([compiler] + args)
        return

    # 1. Discovery Pass via Regex on Preprocessed Output
    discovery_args = get_compiler_args(compiler)
    user_args = []
    for a in args:
        if a.startswith("-I") or a.startswith("-D") or a.startswith("-std="):
            user_args.append(a)
    discovery_args.extend(user_args)

    all_usages = set()
    for src in source_files:
        pp_cmd = [compiler] + user_args + ["-E", src]
        res = subprocess.run(pp_cmd, capture_output=True, text=True)
        if res.returncode != 0:
            continue

        patterns = [
            r"xyz::protocol\s*<\s*([^>]+)\s*>",
            r"xyz::protocol_view\s*<\s*([^>]+)\s*>",
        ]
        for p in patterns:
            for match in re.finditer(p, res.stdout):
                interface = match.group(1).strip()
                all_usages.add(interface)

    if not all_usages:
        res = subprocess.run([compiler] + args)
        sys.exit(res.returncode)

    # 2. Resolve and Generate Pass
    index = clang.cindex.Index.create()
    template_path = os.path.join(os.path.dirname(__file__), "protocol.j2")
    if "-DXYZ_PROTOCOL_GENERATE_MANUAL_VTABLE=ON" in args:
        template_path = os.path.join(
            os.path.dirname(__file__), "protocol_manual_vtable.j2"
        )

    generated_code = []
    for interface in all_usages:
        dummy_content = (
            f'#include "{source_files[0]}"\nusing dummy_usage = {interface};'
        )
        with tempfile.NamedTemporaryFile(mode="w", suffix=".cc") as dummy_src:
            dummy_src.write(dummy_content)
            dummy_src.flush()
            tu = index.parse(dummy_src.name, args=discovery_args)

            target_class_name = interface.split("::")[-1]
            header_path = None
            for node in tu.cursor.walk_preorder():
                if node.kind in [
                    clang.cindex.CursorKind.CLASS_DECL,
                    clang.cindex.CursorKind.STRUCT_DECL,
                ]:
                    if node.spelling == target_class_name:
                        if node.location.file:
                            header_path = node.location.file.name
                            break

            if header_path:
                header_tu = index.parse(header_path, args=discovery_args)
                code = generate_protocol_from_tu(
                    header_tu, target_class_name, template_path, header_path
                )
                generated_code.append(code)

    # 3. Injection Pass
    with tempfile.NamedTemporaryFile(mode="w", suffix=".h", delete=False) as tmp:
        tmp.write("\n".join(generated_code))
        tmp_name = tmp.name

    try:
        new_args = args + ["-include", tmp_name, "-DXYZ_PROTOCOL_COMPILER_WRAPPER"]
        res = subprocess.run([compiler] + new_args)
        sys.exit(res.returncode)
    finally:
        if os.path.exists(tmp_name):
            os.remove(tmp_name)


if __name__ == "__main__":
    main()
