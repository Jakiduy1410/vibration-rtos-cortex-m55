"""Run the three functional scenarios and validate raw UART evidence.

Usage: python verify_reconciled.py --qemu /path/to/qemu-system-arm
No latency, CPU-percentage or RTA result is inferred from this run.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--qemu', default=shutil.which('qemu-system-arm'))
    ap.add_argument('--elf', default='build/firmware.elf')
    args = ap.parse_args()
    if not args.qemu:
        ap.error('Provide --qemu or put qemu-system-arm on PATH')
    out = ROOT/'results/reconciled'
    out.mkdir(parents=True, exist_ok=True)
    cmd = [args.qemu, '-machine', 'mps3-an547', '-cpu', 'cortex-m55',
           '-display', 'none', '-monitor', 'none', '-serial', 'stdio',
           '-semihosting', '-kernel', args.elf]
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=45)
    log = result.stdout.decode('utf-8', errors='replace')
    (out/'simulation.log').write_text(log, encoding='utf-8')
    (out/'stderr.log').write_bytes(result.stderr)
    rows = {}
    for line in log.splitlines():
        if line.startswith('[COUNTS]'):
            v = {k:int(n) for k,n in re.findall(r'(\w+)=(\d+)', line)}
            rows[v.pop('scenario')] = v
    checks = {
        'process_exit_zero': result.returncode == 0,
        'no_fail_marker': '[FAIL]' not in log and '[FATAL]' not in log and '[ASSERT]' not in log,
        'golden_four_pass': len(re.findall(r'-> \[PASS\] \[ok\]', log)) == 4,
        'three_scenarios_complete': all(f'KICH BAN {i} HOAN THANH: [PASS]' in log for i in (1,2,3)),
        'fft_preemption_and_resume': '[PREEMPTION] IRQ -> Alarm while FFT active -> FFT resumed; RMS/bin valid [PASS]' in log,
        'priority_inheritance': 'PRIORITY INHERITANCE TEST COMPLETED [ok]' in log,
        'stack_six_tasks': '[STACK_CHECK] [PASS]' in log and len(re.findall(r'^\[STACK\]', log,re.M)) == 6,
    }
    if len(rows) == 4:
        for i,v in rows.items():
            checks[f'blocks_conserved_{i}'] = v['generated_blocks'] == v['processed_blocks'] + v['isr_overrun_count'] + v['queue_dropped_blocks']
            checks[f'normal_logs_conserved_{i}'] = v['normal_logs_generated'] == v['normal_logs_written'] + v['normal_logs_dropped']
            checks[f'alarm_logs_conserved_{i}'] = v['alarm_logs_generated'] == v['alarm_logs_written'] + v['alarm_logs_dropped']
        checks['phase_totals_match'] = all(rows[0][k] == sum(rows[i][k] for i in (1,2,3)) for k in rows[0])
        checks['scenario1_correct_detection'] = rows[1]['alarm_actuate_count'] == 2 and rows[1]['processed_blocks'] == 31
        checks['scenario3_overload_observed'] = rows[3]['isr_overrun_count'] > 0 and rows[3]['normal_logs_dropped'] > 0
        checks['uart_records_match_written'] = log.count('[ALARM ENTRY #') == rows[0]['alarm_logs_written'] and len(re.findall(r'\[ENTRY #',log)) == rows[0]['normal_logs_written']
    else:
        checks['all_counter_rows_present'] = False
    stacks = [dict(task=n,alloc=int(a),free=int(f),used=int(u)) for n,a,f,u in re.findall(r'\[STACK\] task=(\w+) alloc=(\d+) free=(\d+) used=(\d+)',log)]
    heap = re.search(r'\[HEAP\] total=(\d+) free=(\d+) minimum_free=(\d+)',log)
    summary = {
        'qemu_version':subprocess.check_output([args.qemu,'--version'],text=True).splitlines()[0],
        'command':cmd, 'returncode':result.returncode,
        'elf_sha256':hashlib.sha256((ROOT/args.elf).read_bytes()).hexdigest(),
        'source_sha256':hashlib.sha256((ROOT/'src/main.c').read_bytes()).hexdigest(),
        'baseline_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'counts':rows,'stacks':stacks,
        'heap': dict(zip(['total','free','minimum_free'],map(int,heap.groups()))) if heap else None,
        'checks':checks,'all_passed':all(checks.values()),
        'scope':'Functional behavior and observed memory only; no quantitative timing or physical STM32 claims.'
    }
    (out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False,indent=2))
    return 0 if summary['all_passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
