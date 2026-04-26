"""Script to generate C++ protocol headers from interface definitions."""

import argparse
import hashlib
import os
import re
import subprocess
import sys
from typing import Any
from typing import List

import clang.cindex
from jinja2 import Environment
from jinja2 import FileSystemLoader
from jinja2 import select_autoescape
from xyz.cppmodel import Model

# Try to set the library path explicitly for environments like Bazel
# where LD_LIBRARY_PATH isn't carried over
try:
    import clang.native

    clang.cindex.Config.set_library_path(os.path.dirname(clang.native.__file__))
except Exception:
    pass


def get_method_signature(m: Any) -> str:
    """Generate a string signature for a method."""
    args = ",".join(a.type.name for a in m.arguments)
    constness = "const" if m.is_const else ""
    return f"{m.name}({args}){constness}"


def get_compiler_args(compiler: str = "c++") -> List[str]:
    """Get system include paths from the compiler."""
    args = ["-x", "c++", "-std=c++20"]
    # Ask the system compiler for its standard include paths
    result = subprocess.run(
        [compiler, "-E", "-x", "c++", "-", "-v"],
        input="",
        capture_output=True,
        text=True,
        check=True,
    )

    in_include_section = False
    for line in result.stderr.splitlines():
        # On macOS, the compiler will also list framework directories.
        # We want to ignore those since they won't be relevant for our parsing
        # and could cause issues if we try to include them.
        if "(framework directory)" in line:
            continue
        if line.startswith("#include <...> search starts here:"):
            in_include_section = True
            continue
        if line.startswith("End of search list."):
            break

        if in_include_section:
            path = line.strip()
            args.append(f"-I{path}")

    return args


def mangle_identifier(name: str) -> str:
    """Mangle C++ identifiers (especially operators) for use in C++ code."""
    if name.startswith("operator"):
        op = name[len("operator") :].strip()
        mapping = {
            "+": "plus",
            "-": "minus",
            "*": "star",
            "/": "slash",
            "%": "percent",
            "^": "caret",
            "&": "amp",
            "|": "pipe",
            "~": "tilde",
            "!": "excl",
            "=": "equal",
            "<": "lt",
            ">": "gt",
            "+=": "plus_equal",
            "-=": "minus_equal",
            "*=": "star_equal",
            "/=": "slash_equal",
            "%=": "percent_equal",
            "^=": "caret_equal",
            "&=": "amp_equal",
            "|=": "pipe_equal",
            "<<": "lshift",
            ">>": "rshift",
            "<<=": "lshift_equal",
            ">>=": "rshift_equal",
            "==": "equal_equal",
            "!=": "not_equal",
            "<=": "lte",
            ">=": "gte",
            "<=>": "spaceship",
            "&&": "amp_amp",
            "||": "pipe_pipe",
            "++": "plus_plus",
            "--": "minus_minus",
            ",": "comma",
            "->*": "arrow_star",
            "->": "arrow",
            "()": "call",
            "[]": "subscript",
        }
        if op in mapping:
            return f"__operator__{mapping[op]}__"
        return "operator_" + re.sub(r"[^a-zA-Z0-9_]", "_", op)

    return re.sub(r"[^a-zA-Z0-9_]", "_", name)


def main() -> None:
    """Parse interface and generate protocol header."""
    parser = argparse.ArgumentParser()
    parser.add_argument("input", help="Input header file")
    parser.add_argument("output", help="Output header file")
    parser.add_argument("--template", help="Jinja template file", default="protocol.j2")
    parser.add_argument(
        "--class_name", help="Class name to generate protocol for", required=True
    )
    parser.add_argument(
        "--compiler", help="Compiler to use for system include discovery", default="c++"
    )
    parser.add_argument(
        "--header",
        help="Header file to include in the generated protocol",
        required=True,
    )
    args = parser.parse_args()

    compiler_args = get_compiler_args(compiler=args.compiler)

    index = clang.cindex.Index.create()
    tu = index.parse(args.input, args=compiler_args)

    try:
        model = Model(tu)
    except ValueError as e:
        print(f"Error parsing {args.input}: {e}", file=sys.stderr)
        sys.exit(1)

    # Find target class
    target_class = None
    for c in model.classes:
        if c.name == args.class_name:
            target_class = c
            break

    if not target_class:
        print(f"Class {args.class_name} not found in {args.input}", file=sys.stderr)
        sys.exit(1)

    template_dir = os.path.dirname(os.path.abspath(args.template))
    template_name = os.path.basename(args.template)

    # Setup Jinja2
    env = Environment(
        loader=FileSystemLoader(template_dir), autoescape=select_autoescape()
    )
    env.filters["mangle"] = mangle_identifier
    template = env.get_template(template_name)

    method_guids = [
        hashlib.md5(get_method_signature(m).encode()).hexdigest()[:8]
        for m in target_class.methods
    ]

    # Render
    result = template.render(
        c=target_class, method_guids=method_guids, header=args.header
    )

    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    with open(args.output, "w") as f:
        f.write(result)

    # Format the output file using clang-format
    subprocess.run(["clang-format", "-i", args.output], check=True)


if __name__ == "__main__":
    main()
