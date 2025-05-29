#!/usr/bin/env python3
import subprocess
import tempfile

# Create a very simple LP problem
lp_content = """Maximize
obj: x
Subject To
c1: x <= 5
Bounds
x >= 0
End"""

print("Testing simple LP:")
print(lp_content)
print("-" * 30)

# Write to temp file and solve
with tempfile.NamedTemporaryFile('w', suffix='.lp', delete=False) as f:
    f.write(lp_content)
    lp_path = f.name

sol_path = lp_path + '.sol'

try:
    result = subprocess.run(['glpsol', '--cpxlp', lp_path, '-o', sol_path], 
                          capture_output=True, text=True, check=True)
    print("GLPK stdout:", result.stdout)
    print("SUCCESS: Basic LP works!")
    
    # Read solution
    with open(sol_path) as f:
        print("Solution:")
        print(f.read())
        
except subprocess.CalledProcessError as e:
    print(f"GLPK failed: {e.returncode}")
    print(f"stderr: {e.stderr}")
    print(f"stdout: {e.stdout}") 