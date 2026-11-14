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
    # Parse DWT check & timing lines
    dwt_mono_m = re.search(r'\[DWT_CHECK\] monotonic reads=(\d+) decreases=(\d+)', log)
    dwt_calib_m = re.search(r'\[DWT_CHECK\] calib ticks=(\d+) expected=(\d+) actual=(\d+) within_tolerance=(true|false)', log)
    dwt_fail_m = re.search(r'\[DWT_CHECK\] FAIL (.*)', log)

    s2_m = re.search(r'\[S2_TIMING\]\s+alarm_latency_cycles=(\d+)\s+alarm_latency_us=(\d+)\s+fft_cycles=(\d+)\s+fft_us=(\d+)\s+alarm_exec_cycles=(\d+)\s+alarm_exec_us=(\d+)\s+mutex_block_cycles=(\d+)\s+mutex_block_us=(\d+)', log)
    s3_m = re.search(r'\[S3_TIMING\]\s+blocks=(\d+)\s+max_wait_us=(\d+)\s+mean_wait_us=(\d+)\s+max_resp_us=(\d+)\s+mean_resp_us=(\d+)\s+deadline_miss=(\d+)\s+max_cpu_us=(\d+)\s+mean_cpu_us=(\d+)\s+throughput_bps=([0-9.]+)', log)

    dwt_mono_pass = bool(dwt_mono_m and int(dwt_mono_m.group(2)) == 0)
    dwt_calib_pass = bool(dwt_calib_m and dwt_calib_m.group(4) == 'true' and int(dwt_calib_m.group(3)) > 0)
    dwt_check_passed = bool(dwt_mono_pass and dwt_calib_pass and not dwt_fail_m)

    checks = {
        'process_exit_zero': result.returncode == 0,
        'no_fail_marker': '[FAIL]' not in log and '[FATAL]' not in log and '[ASSERT]' not in log,
        'golden_four_pass': len(re.findall(r'-> \[PASS\] \[ok\]', log)) == 4,
        'three_scenarios_complete': all(f'KICH BAN {i} HOAN THANH: [PASS]' in log for i in (1,2,3)),
        'fft_preemption_and_resume': '[PREEMPTION] IRQ -> Alarm while FFT active -> FFT resumed; RMS/bin valid [PASS]' in log,
        'priority_inheritance': 'PRIORITY INHERITANCE TEST COMPLETED [ok]' in log,
        'stack_six_tasks': '[STACK_CHECK] [PASS]' in log and len(re.findall(r'^\[STACK\]', log,re.M)) == 6,
        'dwt_monotonic_pass': dwt_mono_pass,
        'dwt_calib_pass': dwt_calib_pass,
        'dwt_check_passed': dwt_check_passed,
    }

    if s2_m:
        s2_vals = {
            'alarm_latency_cycles': int(s2_m.group(1)),
            'alarm_latency_us': int(s2_m.group(2)),
            'fft_cycles': int(s2_m.group(3)),
            'fft_us': int(s2_m.group(4)),
            'alarm_exec_cycles': int(s2_m.group(5)),
            'alarm_exec_us': int(s2_m.group(6)),
            'mutex_block_cycles': int(s2_m.group(7)),
            'mutex_block_us': int(s2_m.group(8)),
        }
        checks['s2_timing_valid'] = (
            0 <= s2_vals['alarm_latency_us'] < 100000 and
            0 <= s2_vals['fft_us'] < 100000 and
            0 <= s2_vals['mutex_block_us'] < 100000
        )
    else:
        s2_vals = None

    if s3_m:
        s3_vals = {
            'blocks': int(s3_m.group(1)),
            'max_wait_us': int(s3_m.group(2)),
            'mean_wait_us': int(s3_m.group(3)),
            'max_resp_us': int(s3_m.group(4)),
            'mean_resp_us': int(s3_m.group(5)),
            'deadline_miss': int(s3_m.group(6)),
            'max_cpu_us': int(s3_m.group(7)),
            'mean_cpu_us': int(s3_m.group(8)),
            'throughput_bps': float(s3_m.group(9)),
        }
        checks['s3_timing_valid'] = (
            0 <= s3_vals['mean_cpu_us'] < 100000 and
            0 <= s3_vals['max_resp_us'] < 1000000
        )
    else:
        s3_vals = None

    timing_info = {
        'dwt_check': {
            'present': bool(dwt_mono_m and dwt_calib_m),
            'monotonic_reads': int(dwt_mono_m.group(1)) if dwt_mono_m else None,
            'monotonic_decreases': int(dwt_mono_m.group(2)) if dwt_mono_m else None,
            'calib_ticks': int(dwt_calib_m.group(1)) if dwt_calib_m else None,
            'expected_cycles': int(dwt_calib_m.group(2)) if dwt_calib_m else None,
            'actual_cycles': int(dwt_calib_m.group(3)) if dwt_calib_m else None,
            'within_tolerance': (dwt_calib_m.group(4) == 'true') if dwt_calib_m else False,
            'passed': dwt_check_passed,
            'fail_reason': dwt_fail_m.group(1).strip() if dwt_fail_m else None,
        },
        'scenario2': s2_vals,
        'scenario3': s3_vals,
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
        'timing': timing_info,
        'checks':checks,'all_passed':all(checks.values()),
        'scope':'Functional behavior and observed memory only; no quantitative timing or physical STM32 claims.'
    }
    (out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False,indent=2))
    return 0 if summary['all_passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
