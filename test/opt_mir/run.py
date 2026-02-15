#!/usr/bin/env python3
import os
import sys
import subprocess
import glob

def main():
    # Helper script to run all .r files in this directory through ir_pipeline
    # and generate .ir files.

    script_dir = os.path.dirname(os.path.abspath(__file__))
    # Assuming build dir is relative to project root
    project_root = os.path.dirname(os.path.dirname(script_dir))
    
    # Default path, can be overridden if needed
    compiler_bin = os.path.join(project_root, "build/ninja-debug/cmd/ir_pipeline")
    
    if not os.path.isfile(compiler_bin):
        print(f"Error: Compiler binary not found at {compiler_bin}")
        print("Please build the project first or check the path.")
        sys.exit(1)

    r_files = glob.glob(os.path.join(script_dir, "*.r"))
    if not r_files:
        print("No .r files found in", script_dir)
        sys.exit(0)

    print(f"Found {len(r_files)} test files.")
    print(f"Using compiler: {compiler_bin}")
    print("-" * 40)

    for r_file in sorted(r_files):
        filename = os.path.basename(r_file)
        print(f"Running {filename}...", end=" ", flush=True)
        
        # ir_pipeline <input> [output]
        # We want output to be same name but .ir extension
        # The ir_pipeline tool writes Opt MIR to the output file (argv[2]).
        
        output_file = os.path.splitext(r_file)[0] + ".ir"
        
        try:
            result = subprocess.run(
                [compiler_bin, r_file, output_file],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False
            )
            
            if result.returncode == 0:
                print("OK")
                # verify .ir exists
                if os.path.exists(output_file):
                     # print(f"  Generated {os.path.basename(output_file)}")
                     pass
                else:
                    print(f"  Warning: .ir file not found at {output_file}")
            else:
                print("FAIL")
                print("  Stderr:", result.stderr.strip())
                print("  Stdout:", result.stdout.strip())

        except Exception as e:
            print(f"ERROR: {e}")

if __name__ == "__main__":
    main()
