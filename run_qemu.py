import subprocess
import sys
import os

env = os.environ.copy()
env['PATH'] = r'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + env.get('PATH', '')

cmd = [
    'qemu-system-arm',
    '-machine', 'mps3-an547',
    '-cpu', 'cortex-m55',
    '-nographic',
    '-semihosting',
    '-serial', 'stdio',
    '-kernel', 'build/firmware.elf'
]

print("Launching QEMU:", " ".join(cmd))
try:
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=25, env=env)
    print(f"Process completed with exit code: {proc.returncode}")
    print("=== QEMU STDOUT ===")
    print(proc.stdout)
    if proc.stderr:
        print("=== QEMU STDERR ===")
        print(proc.stderr)
    
    # Save stdout to results/03_simulation_logs.txt
    with open('results/03_simulation_logs.txt', 'w', encoding='utf-8') as f:
        f.write(proc.stdout)
        if proc.stderr:
            f.write("\n=== STDERR ===\n" + proc.stderr)
    print("Saved logs to results/03_simulation_logs.txt")
except subprocess.TimeoutExpired as e:
    print("Process timed out after 12s!")
    out = e.stdout if e.stdout else ""
    print("Partial STDOUT:\n", out)
    with open('results/03_simulation_logs.txt', 'w', encoding='utf-8') as f:
        f.write(out)
