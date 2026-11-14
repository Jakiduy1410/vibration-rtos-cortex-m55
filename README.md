# Giám sát rung động C/FreeRTOS trên Cortex-M55

Bốn tác vụ: Alarm (5), Acquisition (4), Processing (3), Logging (2); kernel có Idle và TmrSvc. Nguồn mẫu tổng hợp 8 kHz logic, 512 mẫu/khối; nhịp danh định 64 ms. Hai buffer DTCM được giữ bằng cờ bận; queue truyền mô tả/con trỏ. Nguồn bỏ lượt mới khi buffer bận. Queue mô tả gửi không chặn, đếm riêng nếu đầy.

## Xây dựng và chạy

Cần GNU Arm GCC và QEMU trên PATH. Chạy `mingw32-make` (hoặc `make`), sau đó `python verify_reconciled.py --qemu <qemu-system-arm>`. Cấu hình CPU/SysTick là 32 MHz (`configCPU_CLOCK_HZ = 32000000`), APB của AN547 là 25 MHz. Chi tiết và hash ELF được lưu trong `results/reconciled/summary.json`.

## Nguồn đo thời gian DWT CYCCNT và kiểm chứng

Hệ thống tích hợp đồng hồ chu kỳ DWT CYCCNT làm nguồn đo thời gian độ phân giải cao (`dwt_cycles()`), khởi tạo qua `DEMCR.TRCENA` và `DWT_CTRL.CYCCNTENA`. Trước khi kịch bản 1 bắt đầu, hệ thống thực hiện hai bước kiểm chứng bắt buộc:
1. **Tính đơn điệu (Monotonicity):** Đọc CYCCNT 10.000 lần liên tiếp, đếm số lần giảm (phải bằng 0).
2. **Hiệu chuẩn với SysTick (Calibration):** Đo ΔCYCCNT trong 100 tick RTOS (100 ms). Với clock 32 MHz, kỳ vọng ΔCYCCNT ≈ 3.200.000 chu kỳ (dung sai ±1 tick: 3.168.000–3.232.000 chu kỳ) và CYCCNT phải tăng (`actual > 0`).

*Lưu ý về môi trường mô phỏng QEMU MPS3 AN547:*
QEMU hiện tại không hiện thực hóa khối phần cứng DWT cho profile ARM Cortex-M (truy cập thanh ghi DWT đọc về 0, ghi bị bỏ qua). Do đó trên QEMU, kiểm tra hiệu chuẩn phát hiện CYCCNT đứng yên (`actual=0`) và dừng chương trình ngay lập tức với `[DWT_CHECK] FAIL` theo đúng đặc tả Mục 2c để tránh sinh hoặc bịa số đo sai lệch. Trên phần cứng thật (hoặc trình giả lập hỗ trợ DWT), bước này sẽ PASS và tiếp tục chạy đo đạc ba kịch bản.

## Ba kịch bản

1. **Kịch bản 1 (Danh định):** Cửa sổ 2000 ms, 31 khối, phát hiện đúng hai khối bất thường #6 (lỗi ổ bi 300 Hz) và #14 (mất cân bằng 100 Hz).
2. **Kịch bản 2 (Chiếm quyền & Thừa kế ưu tiên):** Bài thử dàn dựng có chủ đích; kích hoạt ngắt phần cứng nhân tạo `Interrupt1_IRQn` ngay giữa 2 giai đoạn của RFFT (giữa `arm_cfft_f32` và `stage_rfft_f32`), kiểm tra tác vụ Alarm chiếm quyền Processing tức thì và cập nhật đầu ra trước khi FFT tiếp tục. Phần B là bài thử tranh chấp `xLogMutex` riêng biệt giữa Logging (2) và Processing (3) với sự can thiệp của Acquisition (4) để xác minh cơ chế Priority Inheritance của FreeRTOS.
3. **Kịch bản 3 (Quá tải & Giới hạn tài nguyên):** Nhịp tạo khối tăng vọt từ 64 ms lên 4 ms burst mode, Processing giả lập độ trễ 6 tick (`vTaskDelay`) và log mọi khối để tạo nghẽn hàng đợi/buffer (Drop-Oldest và DMA overrun). Đây là hiện tượng nghẽn hàng đợi và bộ đệm, không phải nghẽn do quá tải tính toán CPU.

## Phạm vi kết quả & Tuyên bố kỹ thuật

- Dùng **results/reconciled/** làm nguồn kết quả. Dùng `verify_reconciled.py` để chạy QEMU và kiểm tra log UART độc lập.
- **Không quy đổi số chu kỳ QEMU thành số đo phần cứng STM32N657X0:** Mọi số liệu chu kỳ hay thời gian (nếu có trên môi trường hỗ trợ) đều là số chu kỳ mô phỏng trên QEMU MPS3 AN547, không đại diện cho thời gian thực thi trên phần cứng STM32N657X0 thật.
- Không khôi phục bộ đo SysTick cũ (`get_system_cycles`, TRACE buffer, TASK_METRIC, `vTaskGetRunTimeStats`).
- Cảnh báo cập nhật biến đầu ra mô phỏng (`g_alarm_output_state = 0x07`), không điều khiển chân GPIO STM32 vật lý.
- Ngưỡng phổ dùng biên độ một phía chuẩn hóa; peak là `max(abs(sample))`. Heap_5 dùng một vùng tĩnh 64 KiB trong SRAM.
