# Kết quả kiểm chứng nguồn đo thời gian DWT CYCCNT & Mô phỏng QEMU

Môi trường: QEMU emulator version 11.0.1 (MPS3 AN547, Cortex-M55)
Target Clock: SYSCLK 32 MHz, Tick FreeRTOS 1.000 Hz (1 tick = 1 ms = 32.000 chu kỳ)

## 1. Kết quả kiểm chứng nguồn đo DWT CYCCNT (§2 Task-dwt)

Trước khi kịch bản 1 bắt đầu, hệ thống đã thực hiện kiểm chứng DWT CYCCNT theo đúng đặc tả Mục 2:

### 2a. Tính đơn điệu (Monotonicity)
- Đọc 10.000 lần liên tiếp trong vòng lặp kín.
- Kết quả: `[DWT_CHECK] monotonic reads=10000 decreases=0` (PASS đơn điệu).

### 2b. Hiệu chuẩn với SysTick (Calibration)
- Đo ΔCYCCNT qua 100 tick RTOS (100 ms) bằng `xTaskGetTickCount()`.
- Kỳ vọng: 100 × 32.000 = 3.200.000 chu kỳ (dung sai ±32.000: [3.168.000 .. 3.232.000]).
- Thực tế đo được: `actual=0` chu kỳ.
- Kết quả: `[DWT_CHECK] calib ticks=100 expected=3200000 actual=0 within_tolerance=false`.

### 2c. Kết luận dừng theo đặc tả (§2c)
- **Lý do dừng:** Thanh ghi `DWT->CYCCNT` (0xE0001004) và `CoreDebug->DEMCR` (0xE000EDFC) trong QEMU MPS3 AN547 không được triển khai (QEMU chỉ mô phỏng tập lệnh mức TCG, không mô phỏng khối phần cứng DWT/CoreSight cho Cortex-M). Mọi thao tác ghi bị bỏ qua và đọc luôn trả về 0.
- **Dòng log xuất hiện:** `[DWT_CHECK] FAIL CYCCNT is not incrementing (actual=0, QEMU does not implement DWT CYCCNT)`
- **Xử lý:** Đúng theo yêu cầu Mục 2c và chỉ thị người giao việc ("nếu kiểm chứng fail thì dừng và báo lại, không được bịa số; không tự sửa bằng cách đổi sang nguồn đo khác; không commit số đo"), chương trình đã kết thúc với mã lỗi semihosting và không tiếp tục chạy 3 kịch bản nhằm bảo đảm tính trung thực học thuật tuyệt đối.

---

## 2. Bảng đối chiếu nhanh 3 giá trị theo yêu cầu

| Mục đối chiếu | Giá trị yêu cầu | Kết quả thực tế trên QEMU AN547 | Ghi chú |
|---|---|---|---|
| **alarm_latency_us (Kịch bản 2)** | Độ trễ cảnh báo (µs) | **N/A** (Dừng tại [DWT_CHECK] FAIL) | Không bịa số đo khi DWT không chạy |
| **mean_cpu_us (Kịch bản 3)** | Thời gian CPU trung bình (µs) | **N/A** (Dừng tại [DWT_CHECK] FAIL) | Không bịa số đo khi DWT không chạy |
| **max_resp_us (Kịch bản 3)** | Thời gian đáp ứng cực đại (µs) | **N/A** (Dừng tại [DWT_CHECK] FAIL) | Không bịa số đo khi DWT không chạy |
| **Kết quả [DWT_CHECK]** | Kiểm chứng đơn điệu + hiệu chuẩn | **FAIL** (actual=0 cycles) | Monotonic: decreases=0; Calib: actual=0, within_tolerance=false |

---

## 3. Tuyên bố phạm vi kỹ thuật

- Toàn bộ mã nguồn đo mốc thời gian cho Kịch bản 2 (`t_fft1`, `t_irq_pend`, `t_alarm`, `t_fft2`, `t_block`, `t_acquire`) và Kịch bản 3 (`t_gen`, `t_start`, `t_proc`, `t_end`, deadline miss 64 ms, throughput) đã được triển khai đầy đủ và sẵn sàng hoạt động trên vi điều khiển vật lý hoặc bộ giả lập có hỗ trợ DWT.
- Số đo chu kỳ (nếu có trên môi trường hỗ trợ) là số chu kỳ mô phỏng, **tuyệt đối không được trình bày như số đo phần cứng STM32N657X0**.
- Kịch bản 2 là bài thử dàn dựng (treo ngắt chủ đích giữa 2 giai đoạn RFFT); quá tải Kịch bản 3 là hiện tượng nghẽn hàng đợi/bộ đệm, không phải nghẽn do quá tải tính toán CPU.
