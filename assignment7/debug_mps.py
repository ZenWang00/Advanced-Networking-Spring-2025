#!/usr/bin/env python3
import subprocess
import tempfile

# Try MPS format instead of CPLEX LP
mps_content = """NAME          PROBLEM
ROWS
 N  OBJ
 L  C1
 E  C2
COLUMNS
    X         OBJ       1.0
    X         C1        1.0
    X         C2        1.0
RHS
    RHS1      C1        5.0
    RHS1      C2        3.0
BOUNDS
 LO BND1      X         0.0
ENDATA"""

print("Testing MPS format:")
print(mps_content)
print("-" * 50)

# Write to temp file and solve
with tempfile.NamedTemporaryFile('w', suffix='.mps', delete=False) as f:
    f.write(mps_content)
    mps_path = f.name

sol_path = mps_path + '.sol'

try:
    result = subprocess.run(['glpsol', '--mps', mps_path, '-o', sol_path], 
                          capture_output=True, text=True, check=True)
    print("GLPK stdout:", result.stdout)
    print("SUCCESS: MPS format works!")
    
    # Read solution
    with open(sol_path) as f:
        print("Solution:")
        print(f.read())
        
except subprocess.CalledProcessError as e:
    print(f"GLPK failed: {e.returncode}")
    print(f"stderr: {e.stderr}")
    print(f"stdout: {e.stdout}")
    print(f"MPS file: {mps_path}") 