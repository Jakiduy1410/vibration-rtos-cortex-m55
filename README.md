# Real-Time Vibration Monitoring & Anomaly Alarm System (FreeRTOS / ARM Cortex-M55)

Hệ thống giám sát rung động thời gian thực và cảnh báo bất thường được xây dựng trên **FreeRTOS Kernel** cho vi xử lý **ARM Cortex-M55** (kiến trúc ARMv8.1-M Mainline hỗ trợ Hard-Float FPU và Helium MVE), mô phỏng trên nền tảng **QEMU `mps3-an547`**.

---

## 📌 Kiến Trúc Hệ Thống (Architecture)

### 1. Phân cấp 4 Task & Mức ưu tiên
- **`Task_Alarm` (Priority: 4 - Cao nhất)**: Chờ ngắt khẩn cấp qua Binary Semaphore. Ngay khi được kích hoạt, lập tức cướp quyền (Preempt) CPU để phát tín hiệu báo động khẩn cấp với độ trễ 0 tick ($\sim 2.8\ \mu s$).
- **`Task_SensorSim` (Priority: 3)**: Định kỳ lấy mẫu dữ liệu gia tốc 3 trục $(a_x, a_y, a_z)$, đẩy vào hàng đợi `xVibrationQueue` (dung lượng 10 mẫu).
- **`Task_Process` (Priority: 2)**: Nhận mẫu, tính toán giá trị hiệu dụng $RMS = \sqrt{(a_x^2 + a_y^2 + a_z^2)/3}$ trên Hard FPU kết hợp bộ lọc DSP. Nếu $RMS > 2.5g$, lập tức phát cờ báo động.
- **`Task_Logger` (Priority: 1 - Thấp nhất)**: Chu kỳ 200ms, sử dụng Mutex (Priority Inheritance) in thông số bộ nhớ Heap, trạng thái rớt gói và Stack High Water Mark.

### 2. Ba Kịch Bản Kiểm Thử Thực Nghiệm (Scenarios)
1. **Kịch bản 1 - Hoạt động bình thường & Phát hiện vượt ngưỡng**: Chu kỳ 50ms (20Hz), nhận diện chính xác các đỉnh rung đột biến tại mẫu số 10 và 16 ($RMS \approx 4.18g > 2.50g$).
2. **Kịch bản 2 - Kiểm chứng Preemption**: Chứng minh cơ chế cướp quyền tuyệt đối tại thời điểm $T_0 = 1200\text{ ms}$: `Process: Trigger` $\rightarrow$ `Alarm: Preempted immediately` $\rightarrow$ `Process: Resumed after Alarm finished`.
3. **Kịch bản 3 - Stress Test & Tràn hàng đợi**: Bơm dữ liệu tốc độ cao (500Hz) đối đầu với tác vụ xử lý nặng (125Hz). Ghi nhận chính xác 50 gói bị từ chối (`errQUEUE_FULL`), nhưng mẫu vượt ngưỡng lọt vào vẫn kích hoạt cảnh báo với độ trễ 0 tick.

---

## 📊 Kết Quả Thực Nghiệm & Đo Lường (Metrology)

- **Flash ROM Footprint**: $18,156\text{ Bytes}$ ($\sim 18.1\text{ KB}$ / $512\text{ KB}$ Flash của AN547, chiếm $3.55\%$).
- **RAM Footprint**: $66,568\text{ Bytes}$ (trong đó $65,536\text{ Bytes}$ là FreeRTOS `heap_4.c`).
- **Context Switch Cost**: $28$ câu lệnh Assembly trong `PendSV_Handler`, quản lý đồng bộ **53 thanh ghi** (Core registers + FPU/Helium MVE `s0-s31` + System registers), tiêu tốn $\approx 60-80\text{ cycles}$ ($\sim 2.8\ \mu s$).
- **Stack High Water Mark**:
  - `Task_Alarm (P4)`: Đỉnh sử dụng $172\text{ B}$ / Cấp phát $2048\text{ B}$ (Dự trữ an toàn $91.6\%$).
  - `Task_SensorSim (P3)`: Đỉnh sử dụng $344\text{ B}$ / Cấp phát $2048\text{ B}$ (Dự trữ an toàn $83.2\%$).
  - `Task_Process (P2)`: Đỉnh sử dụng $336\text{ B}$ / Cấp phát $4096\text{ B}$ (Dự trữ an toàn $91.8\%$).
  - `Task_Logger (P1)`: Đỉnh sử dụng $128\text{ B}$ / Cấp phát $4096\text{ B}$ (Dự trữ an toàn $96.9\%$).

---

## 🛠 Hướng Dẫn Biên Dịch & Chạy Mô Phỏng

### Yêu cầu môi trường
- `arm-none-eabi-gcc` $\ge 13.0$ (hỗ trợ Cortex-M55 / ARMv8.1-M)
- `qemu-system-arm` $\ge 9.0$ (hỗ trợ `-M mps3-an547 -cpu cortex-m55`)
- `make`

### Biên dịch:
```bash
make
```

### Chạy mô phỏng tự động trên QEMU:
```bash
python run_qemu.py
```
*(Hệ thống tích hợp Semihosting `qemu_exit(0)` tự động thoát sạch sau khi hoàn tất 3 kịch bản, không treo terminal).*

---

## 📂 Danh Mục Thư Mục & Báo Cáo
- `src/`: Mã nguồn ứng dụng (`main.c`, `uart.c`, `startup_ARMCM55.c`, `system_ARMCM55.c`).
- `include/`: Cấu hình FreeRTOS (`FreeRTOSConfig.h`).
- `results/`:
  - `01_environment_info.txt`: Thông tin phiên bản toolchain GCC, QEMU, GDB.
  - `02_memory_footprint.txt`: Phân tích chi tiết các section `.text`, `.rodata`, `.data`, `.bss`.
  - `03_simulation_logs.txt`: Toàn bộ UART log thực tế của 3 kịch bản.
  - `04_context_switch_analysis.txt`: Disassembly và số lượng lệnh Assembly của `PendSV_Handler`.
  - `05_gdb_register_dump.txt`: Trạng thái các thanh ghi phần cứng thực tế khi Context Switch.
  - `interactive_dashboard.html`: Giao diện Dashboard tương tác trực quan (Logic Analyzer / Tracealyzer).
  - `figures/`: Bộ 4 biểu đồ độ phân giải cao 300 DPI (Timing Diagram, Vibration Waveforms, Stack HWM, Stack Frame Architecture).
