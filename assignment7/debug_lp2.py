#!/usr/bin/env python3
import subprocess
import tempfile

# Create a LP problem similar to our network flow problem
lp_content = """Maximize
obj: ratio_min
Subject To
ccapr1r2: f_r1_r2_1 <= 3
crmax1: r_star_1 <= 10
cratio1: r_star_1 - 10 ratio_min >= 0
cflow1r1: f_r1_r2_1 = r_star_1
cflow1r2: - f_r1_r2_1 = - r_star_1
Bounds
ratio_min >= 0
r_star_1 >= 0
f_r1_r2_1 >= 0
End"""

print("Testing network-like LP:")
print(lp_content)
print("-" * 50)

# Write to temp file and solve
with tempfile.NamedTemporaryFile('w', suffix='.lp', delete=False) as f:
    f.write(lp_content)
    lp_path = f.name

sol_path = lp_path + '.sol'

try:
    result = subprocess.run(['glpsol', '--cpxlp', lp_path, '-o', sol_path], 
                          capture_output=True, text=True, check=True)
    print("GLPK stdout:", result.stdout)
    print("SUCCESS: Network-like LP works!")
    
    # Read solution
    with open(sol_path) as f:
        print("Solution:")
        print(f.read())
        
except subprocess.CalledProcessError as e:
    print(f"GLPK failed: {e.returncode}")
    print(f"stderr: {e.stderr}")
    print(f"stdout: {e.stdout}")
    print(f"LP file: {lp_path}")
    
    # Show line numbers
    print("\nLP file with line numbers:")
    lines = lp_content.split('\n')
    for i, line in enumerate(lines, 1):
        print(f"{i:3}: {line}") 