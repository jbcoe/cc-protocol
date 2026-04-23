"""
Tests for the protocol generation script.

This module contains tests to verify that the generate_protocol.py script
correctly parses C++ interfaces and generates the corresponding protocol
header files. It uses libclang via xyz-cppmodel for structural verification.
"""

import os
import subprocess
import sys
import tempfile
from typing import Generator
from typing import List
from typing import Optional

import clang.cindex
import pytest
from xyz.cppmodel import Model

# Try to set the library path explicitly for environments like Bazel
# where LD_LIBRARY_PATH isn't carried over
try:
    import clang.native

    clang.cindex.Config.set_library_path(os.path.dirname(clang.native.__file__))
except Exception:
    pass


def run_generate_protocol(
    input_path: str,
    output_path: str,
    class_name: str,
    header_name: str,
    template_path: str = "scripts/protocol.j2",
    extra_args: Optional[List[str]] = None,
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


def get_model_from_file(file_path: str) -> Model:
    """
    Parse a C++ file and return its Model representation.

    Args:
        file_path: Path to the C++ file.

    Returns:
        The Model representation of the file.

    """
    index = clang.cindex.Index.create()
    # We use -std=c++20 as it is the target for this project.
    # We include current directory to find protocol.h
    tu = index.parse(file_path, args=["-std=c++20", "-x", "c++", "-I."])
    return Model(tu)


def test_generate_simple_class(temp_dir: str) -> None:
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

    res = run_generate_protocol(input_header, output_header, "Simple", "input.h")
    assert res.returncode == 0, res.stderr
    assert os.path.exists(output_header)

    model = get_model_from_file(output_header)

    # Verify that the callback classes are generated
    cb_classes = [c.name for c in model.classes]
    assert "protocol_view_const_cb_Simple" in cb_classes
    assert "protocol_view_cb_Simple" in cb_classes

    # Check methods in protocol_view_cb_Simple and protocol_view_const_cb_Simple
    cb_class = next(c for c in model.classes if c.name == "protocol_view_cb_Simple")
    const_cb_class = next(
        c for c in model.classes if c.name == "protocol_view_const_cb_Simple"
    )

    all_method_names = [m.name for m in cb_class.methods] + [
        m.name for m in const_cb_class.methods
    ]

    # Names should be mangled with GUIDs, so we check prefix
    assert any(n.startswith("foo_") for n in all_method_names)
    assert any(n.startswith("bar_") for n in all_method_names)


def test_class_not_found(temp_dir: str) -> None:
    """Test that the script fails gracefully when the target class is missing."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write("class Other {};")

    res = run_generate_protocol(input_header, output_header, "Missing", "input.h")
    assert res.returncode != 0
    assert "Class Missing not found" in res.stderr


def test_mangle_operators(temp_dir: str) -> None:
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

    res = run_generate_protocol(input_header, output_header, "Ops", "input.h")
    assert res.returncode == 0, res.stderr

    model = get_model_from_file(output_header)
    # The actual names are protocol_view_cb_Ops and protocol_view_const_cb_Ops
    cb_class = next(c for c in model.classes if c.name == "protocol_view_cb_Ops")
    const_cb_class = next(
        c for c in model.classes if c.name == "protocol_view_const_cb_Ops"
    )

    all_method_names = [m.name for m in cb_class.methods] + [
        m.name for m in const_cb_class.methods
    ]
    assert any(n.startswith("__operator__equal_equal__") for n in all_method_names)
    assert any(n.startswith("__operator__plus_equal__") for n in all_method_names)


def test_manual_vtable_template(temp_dir: str) -> None:
    """Test that the manual vtable template produces a valid vtable structure."""
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
        template_path="scripts/protocol_manual_vtable.j2",
    )
    assert res.returncode == 0, res.stderr

    # In manual vtable mode, we expect structs for vtables
    with open(output_header, "r") as f:
        content = f.read()
        assert "struct const_view_vtable_Simple" in content
        assert "struct view_vtable_Simple" in content


def test_formatting(temp_dir: str) -> None:
    """Test that the output is correctly formatted using clang-format."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        f.write("class Simple { public: void foo() const; };")

    res = run_generate_protocol(input_header, output_header, "Simple", "input.h")
    assert res.returncode == 0

    with open(output_header, "r") as f:
        formatted_content = f.read()

    # Run clang-format manually on the same file and check if it changes
    # If it's already formatted, it should be identical.
    manual_format_res = subprocess.run(
        ["clang-format", output_header], capture_output=True, text=True, check=True
    )
    assert formatted_content == manual_format_res.stdout


def test_malformed_cpp(temp_dir: str) -> None:
    """Test that the script fails with a clear error on semantic C++ errors."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        # Unknown type name
        f.write("class Simple { public: void foo(NoSuchType x) const; };")

    res = run_generate_protocol(input_header, output_header, "Simple", "input.h")
    assert res.returncode != 0
    assert "Error parsing" in res.stderr
    assert "unknown type name 'NoSuchType'" in res.stderr


def test_syntax_error(temp_dir: str) -> None:
    """Test that the script fails with a clear error on C++ syntax errors."""
    input_header = os.path.join(temp_dir, "input.h")
    output_header = os.path.join(temp_dir, "output.h")

    with open(input_header, "w") as f:
        # Syntax error: missing )
        f.write("class Simple { public: void foo(int x ; };")

    res = run_generate_protocol(input_header, output_header, "Simple", "input.h")
    assert res.returncode != 0
    assert "Error parsing" in res.stderr
    assert "expected ')'" in res.stderr


def test_template_not_found(temp_dir: str) -> None:
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
    )
    assert res.returncode != 0


def test_input_not_found(temp_dir: str) -> None:
    """Test error handling when the input header is missing."""
    output_header = os.path.join(temp_dir, "output.h")
    res = run_generate_protocol("non_existent.h", output_header, "Simple", "input.h")
    assert res.returncode != 0


def test_overloaded_functions(temp_dir: str) -> None:
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

    res = run_generate_protocol(input_header, output_header, "Overloaded", "input.h")
    assert res.returncode == 0, res.stderr

    model = get_model_from_file(output_header)
    cb_class = next(c for c in model.classes if c.name == "protocol_view_cb_Overloaded")
    const_cb_class = next(
        c for c in model.classes if c.name == "protocol_view_const_cb_Overloaded"
    )

    # Collect methods from both callback classes
    all_foo_methods = [m for m in cb_class.methods if m.name.startswith("foo_")] + [
        m for m in const_cb_class.methods if m.name.startswith("foo_")
    ]

    # There should be exactly 3 methods starting with foo_
    assert len(all_foo_methods) == 3

    # Check that they have unique mangled names
    mangled_names = [m.name for m in all_foo_methods]
    assert len(set(mangled_names)) == 3


def test_concept_satisfaction(temp_dir: str) -> None:
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

    res = run_generate_protocol(input_header, output_header, "Simple", "input.h")
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

    # Try to find a compiler
    compiler = None
    for c in ["clang++", "g++"]:
        if subprocess.run(["which", c], capture_output=True).returncode == 0:
            compiler = c
            break

    if not compiler:
        pytest.skip("No C++ compiler found")

    flags = ["-std=c++20", "-I.", f"-I{temp_dir}"]

    comp_res = subprocess.run(
        [compiler] + flags + ["-c", test_cc, "-o", os.path.join(temp_dir, "test.o")],
        capture_output=True,
        text=True,
    )

    assert comp_res.returncode == 0, f"Compilation failed:\n{comp_res.stderr}"


def test_concept_operators(temp_dir: str) -> None:
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

    res = run_generate_protocol(input_header, output_header, "Ops", "input.h")
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

    compiler = None
    for c in ["clang++", "g++"]:
        if subprocess.run(["which", c], capture_output=True).returncode == 0:
            compiler = c
            break

    if not compiler:
        pytest.skip("No C++ compiler found")

    flags = ["-std=c++20", "-I.", f"-I{temp_dir}"]
    comp_res = subprocess.run(
        [compiler] + flags + ["-c", test_cc, "-o", os.path.join(temp_dir, "test.o")],
        capture_output=True,
        text=True,
    )

    assert comp_res.returncode == 0, f"Compilation failed:\n{comp_res.stderr}"
