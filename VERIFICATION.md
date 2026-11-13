# Kết quả kiểm chứng cập nhật

QEMU: QEMU emulator version 11.1.0 (v11.1.0-12130-ge470268ff4)
Mã thoát: 0
ELF SHA-256: be021fd01506f3340c21b717df30da12d668d2d15be25fe13bdec9b12d47369a

## Các kiểm tra độc lập

- PASS: process_exit_zero
- PASS: no_fail_marker
- PASS: golden_four_pass
- PASS: three_scenarios_complete
- PASS: fft_preemption_and_resume
- PASS: priority_inheritance
- PASS: stack_six_tasks
- PASS: blocks_conserved_0
- PASS: normal_logs_conserved_0
- PASS: alarm_logs_conserved_0
- PASS: blocks_conserved_1
- PASS: normal_logs_conserved_1
- PASS: alarm_logs_conserved_1
- PASS: blocks_conserved_2
- PASS: normal_logs_conserved_2
- PASS: alarm_logs_conserved_2
- PASS: blocks_conserved_3
- PASS: normal_logs_conserved_3
- PASS: alarm_logs_conserved_3
- PASS: phase_totals_match
- PASS: scenario1_correct_detection
- PASS: scenario3_overload_observed
- PASS: uart_records_match_written

## Số liệu

| Kịch bản | Yêu cầu | Xử lý | Buffer bỏ | Queue bỏ | Log thường tạo/ghi/bỏ | Log cảnh báo tạo/ghi/bỏ |
|---|---:|---:|---:|---:|---|---|
| 1 | 31 | 31 | 0 | 0 | 1/1/0 | 2/2/0 |
| 2 | 2 | 2 | 0 | 0 | 1/1/0 | 0/0/0 |
| 3 | 324 | 202 | 122 | 0 | 201/17/184 | 1/1/0 |
| Tổng | 357 | 235 | 122 | 0 | 203/19/184 | 3/3/0 |

Nguồn: results/reconciled/simulation.log và summary.json. Không kết luận deadline, CPU%, WCET/RTA, lợi ích MVE, DMA vật lý hoặc điện năng. Không có báo động cứng giả chỉ trong 29 khối bình thường S1.
