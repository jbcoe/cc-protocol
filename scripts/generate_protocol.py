import sys
import argparse
import subprocess
from jinja2 import Environment, FileSystemLoader, select_autoescape
import clang.cindex
from xyz.cppmodel import Model

# Try to set the library path explicitly for environments like Bazel where LD_LIBRARY_PATH isn't carried over
try:
    import clang.native
    import os
    clang.cindex.Config.set_library_path(os.path.dirname(clang.native.__file__))
except Exception:
    pass

def get_compiler_args(compiler='c++'):
    args = ['-x', 'c++', '-std=c++20']
    try:
        # Ask the system compiler for its standard include paths
        result = subprocess.run([compiler, '-E', '-x', 'c++', '-', '-v'], 
                                input='', capture_output=True, text=True)
        
        in_include_section = False
        for line in result.stderr.splitlines():
            if line.startswith('#include <...> search starts here:'):
                in_include_section = True
                continue
            elif line.startswith('End of search list.'):
                break
            
            if in_include_section:
                path = line.strip()
                args.append(f'-I{path}')
                
    except Exception as e:
        print(f"Warning: Could not determine system include paths: {e}", file=sys.stderr)
        
    return args

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', help='Input header file')
    parser.add_argument('output', help='Output header file')
    parser.add_argument('--template', help='Jinja template file', default='protocol.j2')
    parser.add_argument('--class_name', help='Class name to generate protocol for')
    parser.add_argument('--compiler', help='Compiler to use for system include discovery', default='c++')
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

    import os
    template_dir = os.path.dirname(os.path.abspath(args.template))
    template_name = os.path.basename(args.template)
    
    # Setup Jinja2
    env = Environment(loader=FileSystemLoader(template_dir), autoescape=select_autoescape())
    template = env.get_template(template_name)

    # Render
    result = template.render(c=target_class)

    with open(args.output, 'w') as f:
        f.write(result)

if __name__ == '__main__':
    main()

