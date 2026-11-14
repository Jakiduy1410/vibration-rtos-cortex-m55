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

    # Parse SysTick timing outputs (Task-systick.md)
    s2_m = re.search(
        r'\[S2_TIMING\]\s+latency_ticks=(\d+)\s+latency_bound_ms=(\d+)\s+fft_ticks=(\d+)\s+mutex_block_ticks=(\d+)',
        log
    )
    s2_timing = None
    if s2_m:
        s2_timing = {
            'latency_ticks': int(s2_m.group(1)),
            'latency_bound_ms': int(s2_m.group(2)),
            'fft_ticks': int(s2_m.group(3)),
            'mutex_block_ticks': int(s2_m.group(4)),
        }

    s3_m = re.search(
        r'\[S3_TIMING\]\s+blocks=(\d+)\s+max_wait_ms=(\d+)\s+mean_wait_ms=([\d\.]+)\s+max_resp_ms=(\d+)\s+mean_resp_ms=([\d\.]+)\s+deadline_miss=(\d+)\s+max_cpu_ms=(\d+)\s+mean_cpu_ms=([\d\.]+)\s+throughput_bps=([\d\.]+)',
        log
    )
    s3_timing = None
    if s3_m:
        s3_timing = {
            'blocks': int(s3_m.group(1)),
            'max_wait_ms': int(s3_m.group(2)),
            'mean_wait_ms': float(s3_m.group(3)),
            'max_resp_ms': int(s3_m.group(4)),
            'mean_resp_ms': float(s3_m.group(5)),
            'deadline_miss': int(s3_m.group(6)),
            'max_cpu_ms': int(s3_m.group(7)),
            'mean_cpu_ms': float(s3_m.group(8)),
            'throughput_bps': float(s3_m.group(9)),
        }

    block_timings = [
        {
            'seq': int(seq),
            'sc': int(sc),
            'gen': int(gen),
            'start': int(start),
            'end': int(end),
        }
        for seq, sc, gen, start, end in re.findall(
            r'\[BLOCK_T\]\s+seq=(\d+)\s+sc=(\d+)\s+gen=(\d+)\s+start=(\d+)\s+end=(\d+)',
            log
        )
    ]

    # SysTick timing verification checks
    checks['s2_timing_valid'] = (s2_timing is not None) and all(v >= 0 for v in s2_timing.values())
    checks['s2_latency_ticks_valid'] = (s2_timing is not None) and (s2_timing['latency_ticks'] <= 2)
    checks['s3_timing_valid'] = (s3_timing is not None) and (
        s3_timing['blocks'] >= 0 and s3_timing['max_wait_ms'] >= 0 and s3_timing['mean_wait_ms'] >= 0.0 and
        s3_timing['max_resp_ms'] >= 0 and s3_timing['mean_resp_ms'] >= 0.0 and s3_timing['deadline_miss'] >= 0 and
        s3_timing['max_cpu_ms'] >= 0 and s3_timing['mean_cpu_ms'] >= 0.0 and s3_timing['throughput_bps'] >= 0.0
    )
    checks['s3_deadline_miss_valid'] = (s3_timing is not None) and (s3_timing['deadline_miss'] <= s3_timing['blocks'])
    checks['block_timings_non_negative'] = (len(block_timings) > 0) and all(
        b['gen'] >= 0 and b['start'] >= 0 and b['end'] >= 0 and b['start'] >= b['gen'] and b['end'] >= b['start']
        for b in block_timings
    )
    checks['block_records_match_processed'] = (len(rows) == 4) and (len(block_timings) == rows[0]['processed_blocks'])

    systick_timing = {
        'resolution': '1 ms (1 SysTick tick)',
        'uncertainty': '±1 ms quantization error',
        'scenario2': s2_timing,
        'scenario3': s3_timing,
        'block_records_count': len(block_timings),
    }

    summary = {
        'qemu_version':subprocess.check_output([args.qemu,'--version'],text=True).splitlines()[0],
        'command':cmd, 'returncode':result.returncode,
        'elf_sha256':hashlib.sha256((ROOT/args.elf).read_bytes()).hexdigest(),
        'source_sha256':hashlib.sha256((ROOT/'src/main.c').read_bytes()).hexdigest(),
        'baseline_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'counts':rows,'stacks':stacks,
        'heap': dict(zip(['total','free','minimum_free'],map(int,heap.groups()))) if heap else None,
        'checks':checks,'all_passed':all(checks.values()),
        'systick_timing': systick_timing,
        'scope':'Functional behavior, observed memory, and 1 ms SysTick timing bounds; no physical STM32 claims.'
    }

    try:
        summary['compiler_version'] = subprocess.check_output(['arm-none-eabi-gcc', '--version'], text=True).splitlines()[0]
    except Exception:
        pass

    try:
        size_out = subprocess.check_output(['arm-none-eabi-size', '-A', '-d', str(ROOT / args.elf)], text=True)
        (out / 'memory_sections.txt').write_text(size_out, encoding='utf-8')
        sections = {}
        for line in size_out.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[0].startswith('.'):
                try:
                    sections[parts[0]] = int(parts[1])
                except ValueError:
                    pass
        if sections:
            summary['sections'] = sections
            load_sections = ['.text', '.ARM.exidx', '.copy.table', '.zero.table', '.dtcm_data', '.data']
            summary['load_image_bytes'] = sum(sections.get(s, 0) for s in load_sections)
    except Exception:
        pass
    (out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False,indent=2))
    return 0 if summary['all_passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
