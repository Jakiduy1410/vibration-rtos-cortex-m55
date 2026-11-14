# Kết quả kiểm chứng cập nhật

QEMU: QEMU emulator version 11.0.1
Mã thoát: 0
ELF SHA-256: cf8ab7b4f27c5e93fe6f72ae219b20b009a26038ef83f9bcf99960da681576c2

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
- PASS: s2_timing_valid
- PASS: s2_latency_ticks_valid
- PASS: s3_timing_valid
- PASS: s3_deadline_miss_valid
- PASS: block_timings_non_negative
- PASS: block_records_match_processed

## Số liệu số đếm

| Kịch bản | Yêu cầu | Xử lý | Buffer bỏ | Queue bỏ | Log thường tạo/ghi/bỏ | Log cảnh báo tạo/ghi/bỏ |
|---|---:|---:|---:|---:|---|---|
| 1 | 31 | 31 | 0 | 0 | 1/1/0 | 2/2/0 |
| 2 | 2 | 2 | 0 | 0 | 1/1/0 | 0/0/0 |
| 3 | 324 | 216 | 108 | 0 | 215/16/199 | 1/1/0 |
| Tổng | 357 | 249 | 108 | 0 | 217/18/199 | 3/3/0 |

## Số liệu đo thời gian bằng SysTick Tick (1 ms)

Nguồn đo: `xTaskGetTickCount()`, độ phân giải 1 ms, sai số lượng tử hóa tối đa ±1 ms. Môi trường: Mô phỏng QEMU MPS3 AN547 (SYSCLK 32 MHz).

### Kịch bản 2: Chiếm quyền & Mutex (`[S2_TIMING]`)
- `latency_ticks`: 0 tick
- `latency_bound_ms`: 1 ms (giới hạn trên thô: độ trễ cảnh báo < 1 ms)
- `fft_ticks`: 0 tick (thời gian tính 2 giai đoạn FFT < 1 ms)
- `mutex_block_ticks`: 0 tick (thời gian chặn mutex trước khi kế thừa ưu tiên giải phóng < 1 ms)

### Kịch bản 3: Quá tải tài nguyên (`[S3_TIMING]`)
- `blocks`: 216 khối đã xử lý
- `max_wait_ms` / `mean_wait_ms`: 6 ms / 3.00 ms (thời gian chờ trong `xBlockQueue`)
- `max_resp_ms` / `mean_resp_ms`: 6 ms / 3.00 ms (thời gian đáp ứng từ `t_gen` đến `t_end`)
- `deadline_miss`: 0 khối (không có khối nào vượt hạn 64 ms)
- `max_cpu_ms` / `mean_cpu_ms`: 0 ms / 0.00 ms (thời gian CPU tính toán FFT + đặc trưng < 1 ms)
- `throughput_bps`: 167.44 khối/giây

### Mốc thời gian từng khối (`[BLOCK_T]`)
- Tổng số bản ghi `[BLOCK_T]` xuất ra UART: 249 bản ghi (khớp chính xác đẳng thức với tổng số khối đã xử lý `processed_blocks = 249`).
- Mỗi bản ghi cung cấp: `seq`, `sc` (kịch bản), `gen`, `start`, `end`.

## Lưu ý kỹ thuật & Giới hạn phạm vi
1. **Đơn vị & Sai số đo:** Toàn bộ số đo là số tick 1 ms trên mô phỏng QEMU AN547 với sai số lượng tử hóa ±1 ms; **không phải số đo micro giây** và **không phải số đo trên phần cứng vật lý STM32N657X0**. Không trình bày giới hạn thô mili giây như số đo micro giây.
2. **Kịch bản 2:** Là bài thử dàn dựng (ngắt phần cứng nhân tạo `Interrupt1_IRQn` kích hoạt chủ đích giữa 2 giai đoạn FFT). Độ trễ cảnh báo chỉ là giới hạn trên thô (< 1–2 ms), chưa xác nhận định lượng ở micro giây.
3. **Kịch bản 3:** Quá tải quan sát được là **nghẽn queue và buffer** (do nhịp DMA 4 ms nhanh hơn tốc độ xả), **không phải nghẽn CPU**.
4. **Định nghĩa mốc `t_end`:** Mốc `t_end` được ghi nhận ngay sau khi hoàn thành tính toán FFT, trích xuất đặc trưng và đưa bản ghi log vào queue — **trước khi gọi `vTaskDelay(6)`**. Do đó `resp = t_end - t_gen` không cộng thêm 6 ms delay nhân tạo, và số khối trễ hạn (`deadline_miss`) được tính theo định nghĩa này.
5. Nguồn dữ liệu: `results/reconciled/simulation.log` và `results/reconciled/summary.json`.
