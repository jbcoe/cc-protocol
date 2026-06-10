"""
Tests for the protocol generation script.

This module contains tests to verify that the generate_protocol.py script
correctly parses C++ interfaces and generates the corresponding protocol
header files. It uses libclang via xyz-cppmodel for structural verification.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from typing import Generator
from typing import List
from typing import Optional

import clang.cindex
import pytest
from xyz.cppmodel import Model

from scripts.generate_protocol import get_compiler_args

# Try to set the library path explicitly for environments like Bazel
# where LD_LIBRARY_PATH isn't carried over
try:
    import clang.native

    clang.cindex.Config.set_library_path(os.path.dirname(clang.native.__file__))
except Exception:
    pass


def get_compiler() -> Optional[str]:
    """Find a C++ compiler on the system."""
    for c in ["clang++", "g++", "c++"]:
        if shutil.which(c):
            return c
    return None


def run_generate_protocol(
    input_path: str,
    output_path: str,
    class_name: str,
    header_name: str,
    template_path: str = "scripts/protocol.j2",
    extra_args: Optional[List[str]] = None,
    compiler: Optional[str] = None,
) -> subprocess.CompletedProcess[str]:
    """
    Run the generate_protocol.py script with the given arguments.

    Args:
        input_path: Path to the input C++ header.
        output_path: Path where the generated header should be written.
        class_name: Name of the class to generate the protocol for.
        header_name: Name of the header to include in the generated code.
        template_path: Path to the Jinja2 template.
        extra_args: Additional command-line arguments.
        compiler: The compiler to use for include discovery.

    Returns:
        The result of the subprocess run.

    """
    cmd = [
        sys.executable,
        "scripts/generate_protocol.py",
        input_path,
        output_path,
        "--class_name",
        class_name,
        "--header",
        header_name,
        "--template",
        template_path,
    ]
    if compiler:
        cmd.extend(["--compiler", compiler])
    if extra_args:
        cmd.extend(extra_args)
    return subprocess.run(cmd, capture_output=True, text=True)


@pytest.fixture
def temp_dir() -> Generator[str, None, None]:
    """
    Fixture that provides a temporary directory for test files.

    Yields:
        The path to the temporary directory.

    """
    with tempfile.TemporaryDirectory() as tmpdir:
        yield tmpdir


@pytest.fixture
def compiler() -> str:
    """Fixture that provides a C++ compiler path."""
    c = get_compiler()
    assert c, "No C++ compiler found on the system"
    return c


def get_model_from_file(file_path: str, compiler: str) -> Model:
    """
    Parse a C++ file and return its Model representation.

    Args:
        file_path: Path to the C++ file.
        compiler: The compiler to use for include discovery.

    Returns:
        The Model representation of the file.

    """
    index = clang.cindex.Index.create()
    args = get_compiler_args(compiler)
    args.append("-I.")
    tu = index.parse(file_path, args=args)
    return Model(tu)


def test_generate_simple_class(temp_dir: str, compiler: str) -> None:
    """
    Test generation for a simple class with basic methods.

    Verifies that the generated concepts and callback classes exist and contain
    the expected methods.
    """
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write(
            """
        namespace test {
        class Simple {
        public:
            void foo() const;
            int bar(int x);
        };
        }
        """
        )

    res = run_generate_protocol(
        input_header, output_header, "Simple", "input.h", compiler=compiler
    )
    assert res.returncode == 0, res.stderr
    assert os.path.exists(output_header)

    model = get_model_from_file(output_header, compiler)

    # Verify that the vtable structs are generated
    vtable_classes = [c.name for c in model.classes]
    assert "const_view_vtable_Simple" in vtable_classes
    assert "view_vtable_Simple" in vtable_classes

    # Check members in view_vtable_Simple and const_view_vtable_Simple
    vtable_class = next(c for c in model.classes if c.name == "view_vtable_Simple")
    const_vtable_class = next(
        c for c in model.classes if c.name == "const_view_vtable_Simple"
    )

    all_member_names = [m.name for m in vtable_class.members] + [
        m.name for m in const_vtable_class.members
    ]

    # Names should be mangled with GUIDs, so we check prefix
    assert any(n.startswith("foo_") for n in all_member_names)
    assert any(n.startswith("bar_") for n in all_member_names)


def test_class_not_found(temp_dir: str, compiler: str) -> None:
    """Test that the script fails gracefully when the target class is missing."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write("class Other {};")

    res = run_generate_protocol(
        input_header, output_header, "Missing", "input.h", compiler=compiler
    )
    assert res.returncode != 0
    assert "Class Missing not found" in res.stderr


def test_mangle_operators(temp_dir: str, compiler: str) -> None:
    """Test that C++ operators are correctly mangled in the generated code."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write(
            """
        class Ops {
        public:
            bool operator==(const Ops&) const;
            Ops& operator+=(const Ops&);
        };
        """
        )

    res = run_generate_protocol(
        input_header, output_header, "Ops", "input.h", compiler=compiler
    )
    assert res.returncode == 0, res.stderr

    model = get_model_from_file(output_header, compiler)
    vtable_class = next(c for c in model.classes if c.name == "view_vtable_Ops")
    const_vtable_class = next(
        c for c in model.classes if c.name == "const_view_vtable_Ops"
    )

    all_member_names = [m.name for m in vtable_class.members] + [
        m.name for m in const_vtable_class.members
    ]
    assert any(n.startswith("__operator__equal_equal__") for n in all_member_names)
    assert any(n.startswith("__operator__plus_equal__") for n in all_member_names)


def test_vtable_structures(temp_dir: str, compiler: str) -> None:
    """Test that the generated code produces manual vtable structures."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write(
            """
        class Simple {
        public:
            void foo() const;
        };
        """
        )

    res = run_generate_protocol(
        input_header,
        output_header,
        "Simple",
        "input.h",
        template_path="scripts/protocol.j2",
        compiler=compiler,
    )
    assert res.returncode == 0, res.stderr

    # We expect structs for manual vtables
    with open(output_header, "r") as f:
        content = f.read()
        assert "struct const_view_vtable_Simple" in content
        assert "struct view_vtable_Simple" in content


def test_formatting(temp_dir: str, compiler: str) -> None:
    """Test that the output is correctly formatted using clang-format."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write("class Simple { public: void foo() const; };")

    res = run_generate_protocol(
        input_header, output_header, "Simple", "input.h", compiler=compiler
    )
    assert res.returncode == 0

    with open(output_header, "r") as f:
        formatted_content = f.read()

    # Run clang-format manually on the same file and check if it changes
    # If it's already formatted, it should be identical.
    manual_format_res = subprocess.run(
        ["clang-format", output_header], capture_output=True, text=True, check=True
    )
    assert formatted_content == manual_format_res.stdout


def test_malformed_cpp(temp_dir: str, compiler: str) -> None:
    """Test that the script fails with a clear error on semantic C++ errors."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        # Unknown type name
        f.write("class Simple { public: void foo(NoSuchType x) const; };")

    res = run_generate_protocol(
        input_header, output_header, "Simple", "input.h", compiler=compiler
    )
    assert res.returncode != 0
    assert "Error parsing" in res.stderr
    assert "unknown type name 'NoSuchType'" in res.stderr


def test_syntax_error(temp_dir: str, compiler: str) -> None:
    """Test that the script fails with a clear error on C++ syntax errors."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        # Syntax error: missing )
        f.write("class Simple { public: void foo(int x ; };")

    res = run_generate_protocol(
        input_header, output_header, "Simple", "input.h", compiler=compiler
    )
    assert res.returncode != 0
    assert "Error parsing" in res.stderr
    assert "expected ')'" in res.stderr


def test_template_not_found(temp_dir: str, compiler: str) -> None:
    """Test error handling when the Jinja2 template is missing."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write("class Simple {};")

    res = run_generate_protocol(
        input_header,
        output_header,
        "Simple",
        "input.h",
        template_path="non_existent.j2",
        compiler=compiler,
    )
    assert res.returncode != 0


def test_input_not_found(temp_dir: str, compiler: str) -> None:
    """Test error handling when the input header is missing."""
    output_header = os.path.join(temp_dir, "output.h")
    res = run_generate_protocol(
        "non_existent.h", output_header, "Simple", "input.h", compiler=compiler
    )
    assert res.returncode != 0


def test_overloaded_functions(temp_dir: str, compiler: str) -> None:
    """
    Test that overloaded functions result in unique mangled names.

    Uses the C++ model to verify that each overload has a corresponding
    entry in the generated callback class.
    """
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write(
            """
        class Overloaded {
        public:
            void foo() const;
            void foo(int x) const;
            void foo(double d);
        };
        """
        )

    res = run_generate_protocol(
        input_header, output_header, "Overloaded", "input.h", compiler=compiler
    )
    assert res.returncode == 0, res.stderr

    model = get_model_from_file(output_header, compiler)
    vtable_class = next(c for c in model.classes if c.name == "view_vtable_Overloaded")
    const_vtable_class = next(
        c for c in model.classes if c.name == "const_view_vtable_Overloaded"
    )

    # Collect members from both vtable structs
    all_foo_members = [m for m in vtable_class.members if m.name.startswith("foo_")] + [
        m for m in const_vtable_class.members if m.name.startswith("foo_")
    ]

    # There should be exactly 3 members starting with foo_
    assert len(all_foo_members) == 3

    # Check that they have unique mangled names
    mangled_names = [m.name for m in all_foo_members]
    assert len(set(mangled_names)) == 3


def test_concept_satisfaction(temp_dir: str, compiler: str) -> None:
    """
    Verify generated concepts using a real C++ compiler.

    This test ensures that the generated static concepts correctly identify
    whether a type satisfies the protocol.
    """
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "protocol_Simple.h")

    with open(input_header, "w") as f:
        f.write(
            """
        class Simple {
        public:
            void foo() const;
            int bar(int x);
        };
        """
        )

    res = run_generate_protocol(
        input_header, output_header, "Simple", "input.h", compiler=compiler
    )
    assert res.returncode == 0, res.stderr

    # Create a C++ file to test the concept
    test_cc = os.path.join(temp_dir, "test.cc")
    with open(test_cc, "w") as f:
        f.write(
            f"""
        #include "{output_header}"

        struct Valid {{
            void foo() const {{}}
            int bar(int x) {{ return x; }}
        }};

        struct InvalidMissing {{
            void foo() const {{}}
        }};

        struct InvalidWrongSig {{
            void foo() {{}} // not const
            int bar(int x) {{ return x; }}
        }};

        static_assert(xyz::protocol_concept_Simple<Valid>);
        static_assert(!xyz::protocol_concept_Simple<InvalidMissing>);
        static_assert(!xyz::protocol_concept_Simple<InvalidWrongSig>);

        int main() {{ return 0; }}
        """
        )

    flags = ["-std=c++20", "-I.", f"-I{temp_dir}"]

    comp_res = subprocess.run(
        [compiler] + flags + ["-c", test_cc, "-o", os.path.join(temp_dir, "test.o")],
        capture_output=True,
        text=True,
    )

    assert comp_res.returncode == 0, f"Compilation failed:\n{comp_res.stderr}"


def test_concept_operators(temp_dir: str, compiler: str) -> None:
    """
    Verify generated concepts for C++ operators using a real compiler.

    Ensures that mangled operator names in the concept correctly match
    the native C++ operators in satisfying types.
    """
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "protocol_Ops.h")

    with open(input_header, "w") as f:
        f.write(
            """
        class Ops {
        public:
            bool operator==(int other) const;
            void operator+=(int other);
        };
        """
        )

    res = run_generate_protocol(
        input_header, output_header, "Ops", "input.h", compiler=compiler
    )
    assert res.returncode == 0, res.stderr

    test_cc = os.path.join(temp_dir, "test.cc")
    with open(test_cc, "w") as f:
        f.write(
            f"""
        #include "{output_header}"

        struct Valid {{
            bool operator==(int) const {{ return true; }}
            void operator+=(int) {{}}
        }};

        struct Invalid {{
            bool operator==(int) {{ return true; }} // not const
        }};

        static_assert(xyz::protocol_concept_Ops<Valid>);
        static_assert(!xyz::protocol_concept_Ops<Invalid>);

        int main() {{ return 0; }}
        """
        )

    flags = ["-std=c++20", "-I.", f"-I{temp_dir}"]
    comp_res = subprocess.run(
        [compiler] + flags + ["-c", test_cc, "-o", os.path.join(temp_dir, "test.o")],
        capture_output=True,
        text=True,
    )

    assert comp_res.returncode == 0, f"Compilation failed:\n{comp_res.stderr}"


def test_protocol_reference(temp_dir: str, compiler: str) -> None:
    """Verify generated protocol matches checked-in reference_interface_protocol.h."""
    import shutil

    shutil.copy(".clang-format", temp_dir)

    reference_header = "reference_interface.h"
    reference_protocol = "reference_interface_protocol.h"
    temp_output = os.path.join(temp_dir, "temp_reference_interface_protocol.h")

    # Run the generator on reference_interface.h
    res = run_generate_protocol(
        reference_header,
        temp_output,
        "ReferenceInterface",
        "reference_interface.h",
        compiler=compiler,
    )
    assert res.returncode == 0, res.stderr

    # Read the expected and generated files
    with open(reference_protocol, "r") as f:
        expected = f.read()
    with open(temp_output, "r") as f:
        actual = f.read()

    assert expected == actual, (
        "Generated protocol does not match reference_interface_protocol.h! "
        "If the generator template has changed, please regenerate "
        "the reference file using:\n"
        "uv run scripts/regenerate_reference_interface_protocol.py"
    )


def test_trailing_newline(temp_dir: str, compiler: str) -> None:
    """Test that the generated code always ends with a trailing newline."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write("class Simple { public: void foo() const; };")

    res = run_generate_protocol(
        input_header, output_header, "Simple", "input.h", compiler=compiler
    )
    assert res.returncode == 0

    with open(output_header, "rb") as f:
        content = f.read()
    assert content.endswith(b"\n")
