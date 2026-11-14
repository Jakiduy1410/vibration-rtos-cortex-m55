# Giám sát rung động C/FreeRTOS trên Cortex-M55

Bốn tác vụ: Alarm (5), Acquisition (4), Processing (3), Logging (2); kernel có Idle và TmrSvc. Nguồn mẫu tổng hợp 8 kHz logic, 512 mẫu/khối; nhịp danh định 64 ms. Hai buffer DTCM được giữ bằng cờ bận; queue truyền mô tả/con trỏ. Nguồn bỏ lượt mới khi buffer bận. Queue mô tả gửi không chặn, đếm riêng nếu đầy.

## Xây dựng và chạy

Cần GNU Arm GCC và QEMU trên PATH. Chạy `mingw32-make` (hoặc `make`), sau đó `python verify_reconciled.py --qemu <qemu-system-arm>`. Cấu hình CPU/SysTick là 32 MHz, APB của AN547 là 25 MHz. Bản đã kiểm tra dùng GCC 13.3.1 xPack và QEMU 11.1.0; chi tiết và hash ELF trong `results/reconciled/summary.json`. Không commit mới; đây là thay đổi cục bộ trên nền 90ce744.

## Ba kịch bản

1. Cửa sổ 2000 ms, hai khối bất thường #6/#14 và kiểm tra luồng dữ liệu.
2. Bài thử dàn dựng: ngắt treo chủ đích (`NVIC_SetPendingIRQ(Interrupt1_IRQn)`) giữa hai giai đoạn FFT, kiểm tra Alarm chạy trước khi FFT trở lại, đồng thời kiểm tra RMS/bin sau tiếp tục; mutex là bài thử chuyên biệt với trình tự Logging 2→3→2.
3. Nhịp yêu cầu 64→4 ms (16 lần), Processing chờ 6 tick và log mỗi khối để tạo nghẽn có chủ đích. Hiện tượng quá tải quan sát được ở đây là **nghẽn queue và buffer**, hoàn toàn **không phải nghẽn CPU** (thời gian xử lý FFT + đặc trưng trên CPU < 1 ms).

## Đo thời gian bằng SysTick Tick (1 ms)

Hệ thống sử dụng số tick FreeRTOS `xTaskGetTickCount()` làm nguồn đo thời gian thực nghiệm:
- **Tần số tick & Sai số lượng tử hóa:** FreeRTOS cấu hình nhịp tick danh định 1.000 Hz (**1 tick = 1 ms**). Mọi mốc thời gian được lượng tử hóa về mili giây nguyên, do đó **mọi khoảng thời gian tính bằng hiệu hai mốc đều có sai số tối đa ±1 ms**.
- **Môi trường giả lập:** Toàn bộ số đo là **số tick trên mô phỏng QEMU MPS3 AN547, không phải số đo trên phần cứng vật lý STM32N657X0**. Không trình bày giới hạn thô mili giây như số đo micro giây.
- **Độ trễ cảnh báo Kịch bản 2:** Đo qua hiệu `t_output - t_irq_pend` (`latency_ticks`, quan sát được 0–1 tick), cho giới hạn trên thô `latency_bound_ms = latency_ticks + 1` (< 1–2 ms). Đây chỉ là **giới hạn trên thô theo độ phân giải tick 1 ms**, **chưa xác nhận định lượng ở cấp micro giây**.
- **Mốc thời gian từng khối và Kịch bản 3:**
  - `t_gen`: Mốc thời gian khi Thu thập lấy khối khỏi double-buffer (`desc.timestamp_ms`).
  - `t_start`: Mốc khi Xử lý lấy mô tả khối từ `xBlockQueue`.
  - `t_end`: Mốc khi Xử lý hoàn tất tính toán FFT, trích xuất đặc trưng và đưa log vào queue — **được lấy TRƯỚC `vTaskDelay(6)` của Kịch bản 3**. Do đó, `resp = t_end - t_gen` **không bao gồm 6 ms delay nhân tạo**, và số khối trễ hạn 64 ms (`deadline_miss`) được đánh giá dựa trên thời gian xử lý thực sự này.
  - Toàn bộ mốc thời gian của từng khối được xả ra UART sau khi Kịch bản 3 hoàn tất dưới dạng: `[BLOCK_T] seq=<n> sc=<1|2|3> gen=<tick> start=<tick> end=<tick>`.

## Phạm vi kết quả

Dùng **results/reconciled/** làm nguồn kết quả mới. Những báo cáo và hình ở results/ gốc là bằng chứng của bản cũ, không dùng cho bản báo cáo đã sửa. Các script generate_reports.py/plot_figures.py cũ không dùng để sinh lại kết quả mới.

Bản mới kiểm tra chức năng, số đếm và HWM; không xuất CPU%, RTA, WCET hoặc độ trễ micro giây từ bộ đếm SysTick cũ. Mục tiêu 10 ms vẫn là yêu cầu chưa xác nhận định lượng. Tickless bật nhưng chưa thử điện năng. Cảnh báo cập nhật biến đầu ra mô phỏng, không điều khiển GPIO STM32 thật. Ngưỡng phổ dùng biên độ một phía chuẩn hóa; peak là max(abs(sample)).

Heap_5 hiện dùng một vùng 64 KiB, không mặc định cần nhiều vùng. Cấp phát stack/TCB tĩnh nằm ngoài heap. Không suy ra không rò rỉ hoặc an toàn tuyệt đối từ một lần chạy. Thư viện CMSIS-DSP 1.10.0 đi cùng gói CMSIS 5.9.0; nguồn có hook FFT phục vụ kiểm thử.
