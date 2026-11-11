import subprocess
import time
import os

env = os.environ.copy()
env['PATH'] = r'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + env.get('PATH', '')

qemu_cmd = [
    'qemu-system-arm',
    '-M', 'mps3-an547',
    '-cpu', 'cortex-m55',
    '-nographic',
    '-kernel', 'build/firmware.elf',
    '-s', '-S'
]

qemu_proc = subprocess.Popen(qemu_cmd, env=env)
time.sleep(1)

gdb_cmd = [
    'gdb-multiarch',
    '-batch',
    '-ex', 'target remote :1234',
    '-ex', 'info registers',
    '-ex', 'x/5i $pc',
    '-ex', 'stepi 10',
    '-ex', 'info registers',
    '-ex', 'x/5i $pc',
    'build/firmware.elf'
]

try:
    gdb_proc = subprocess.run(gdb_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5, env=env)
    print("=== GDB STDOUT ===")
    print(gdb_proc.stdout)
    if gdb_proc.stderr:
        print("=== GDB STDERR ===")
        print(gdb_proc.stderr)
finally:
    qemu_proc.kill()
