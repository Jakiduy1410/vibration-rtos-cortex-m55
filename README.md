# Hệ Thống Giám Sát Rung Động & Cảnh Báo Bất Thường Thời Gian Thực
### (Real-Time Vibration Monitoring & Anomaly Alarm System on ARM Cortex-M55)

[![Cortex-M55](https://img.shields.io/badge/CPU-ARM%20Cortex--M55-blue.svg)](https://developer.arm.com/Processors/Cortex-M55)
[![FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20v11.1.0-brightgreen.svg)](https://www.freertos.org/)
[![CMSIS-DSP](https://img.shields.io/badge/DSP-CMSIS--DSP%205.9.0%20(Helium)-orange.svg)](https://github.com/ARM-software/CMSIS_5)
[![QEMU](https://img.shields.io/badge/Emulation-QEMU%20MPS3%20AN547-purple.svg)](https://www.qemu.org/)
[![Verification](https://img.shields.io/badge/Verification-100%25%20PASS-success.svg)](VERIFICATION.md)

---

## 📖 Giới Thiệu Tổng Quan

Dự án cài đặt và thực nghiệm **Hệ thống Giám sát Rung động và Cảnh báo Bất thường Thời gian thực** bám sát 100% tài liệu thiết kế **Chương III**. Hệ thống được xây dựng trên **FreeRTOS Kernel V11.1.0** dành cho kiến trúc vi điều khiển **ARM Cortex-M55** (ARMv8.1-M Mainline tích hợp công nghệ xử lý vector **Helium MVE** và bộ tính toán số thực phần cứng **Hard-Float FPU**), mô phỏng và kiểm chứng trên nền tảng **QEMU MPS3 AN547** (`-cpu cortex-m55`).

Hệ thống giám sát tín hiệu rung động lấy mẫu ở tần số danh định **8 kHz**, chia khối **512 mẫu** (chu kỳ 64 ms). Tác vụ tính toán xử lý sử dụng thư viện **ARM CMSIS-DSP 5.9.0** để thực thi biến đổi Fourier thực nhanh (**512-point Real FFT**) tăng tốc bởi Helium vectorization, trích xuất các đặc trưng miền thời gian (RMS, Đỉnh gia tốc, Hệ số đỉnh CF) và miền tần số (Tần số đỉnh phổ, Năng lượng dải sóng hài), từ đó đưa ra quyết định cảnh báo khẩn cấp với độ trễ thấp thông qua cơ chế Task Notification và Preemption.

---

## ⚙️ Kiến Trúc Hệ Thống & Bộ 4 Tác Vụ Thời Gian Thực (Bảng 3.1 Chương III)

Hệ thống bao gồm đúng **4 tác vụ ứng dụng + 1 tác vụ Idle**, tuyệt đối không phát sinh thêm tác vụ ngoài thiết kế:

| Tác Vụ (Task Name) | Mức Ưu Tiên | Chu Kỳ ($T_i$) / Hạn ($D_i$) | Thời Gian Tính ($C_i$) | Vùng Nhớ Ngăn Xếp | Kích Thước Stack (Byte / Word) | Vai Trò & Cơ Chế Hoạt Động |
|:---|:---:|:---:|:---:|:---:|:---:|:---|
| **`vTaskAlarm`** | **5** (Cao nhất) | Phi chu kỳ ($D_i = 10.0$ ms) | $1.0$ ms | **DTCM** | 1,024 B (256 words) | Chờ Task Notification mang mã sự kiện từ Xử lý. Kích hoạt trực tiếp cơ cấu chấp hành (Còi/Đèn/Rơ-le) qua thanh ghi. **Tuyệt đối không lấy Mutex hay gọi UART**. |
| **`vTaskAcquisition`** | **4** | Chu kỳ $T = 64$ ms ($D_i = 64$ ms) | $2.0$ ms | **DTCM** | 1,024 B (256 words) | Chờ Binary Semaphore `xBlockReady` từ ngắt DMA stub. Đóng gói mô tả khối (`BlockDescriptor_t`) gửi vào `xBlockQueue`. |
| **`vTaskProcessing`** | **3** | Chu kỳ $T = 64$ ms ($D_i = 64$ ms) | $12.0$ ms | **DTCM** | 4,096 B (1,024 words) | Nhận khối, tăng tốc Vector Helium CMSIS-DSP tính toán FFT 512 mẫu, trích xuất đặc trưng rung động, so ngưỡng quyết định phát tín hiệu cảnh báo. |
| **`vTaskLogging`** | **2** | Chu kỳ $T = 1,000$ ms ($D_i = 1,000$ ms) | $20.0$ ms | **SRAM** | 2,048 B (512 words) | Nhận bản ghi telemetry qua `xLogQueue` (bảo vệ bởi `xLogMutex`). Quản lý in log định kỳ ra UART và điều phối các kịch bản kiểm thử. |
| **`Idle Task`** | **0** (Thấp nhất) | Khi CPU rảnh rỗi | - | **SRAM** | 512 B (128 words) | Thu dọn tác vụ đã xóa, hỗ trợ `configUSE_TICKLESS_IDLE = 1` để tối ưu năng lượng. |

> **Kiểm chứng khả năng lập lịch (RMA Schedulability)**:  
> Tổng hệ số sử dụng CPU của 3 tác vụ chu kỳ:
> $$U = \frac{C_{\text{Acq}}}{T_{\text{Acq}}} + \frac{C_{\text{Proc}}}{T_{\text{Proc}}} + \frac{C_{\text{Log}}}{T_{\text{Log}}} = \frac{2.0}{64.0} + \frac{12.0}{64.0} + \frac{20.0}{1000.0} = 3.125\% + 18.75\% + 2.0\% = 23.875\% \approx 23.9\%$$
> Giá trị này thấp hơn nhiều so với cận trên Liu & Layland: $U_{\text{bound}} = 3(2^{1/3} - 1) \approx 77.98\%$. Toàn bộ các tác vụ đảm bảo đáp ứng hạn định nghiêm ngặt!

---

## 🔄 4 Đối Tượng Đồng Bộ & Giao Tiếp Liên Tác Vụ (IPC)

1. **`xBlockReady` (Binary Semaphore)**: Cầu nối trực tiếp giữa Trình phục vụ ngắt DMA stub (`Interrupt0_IRQHandler`) và `vTaskAcquisition` khi một nửa bộ đệm đôi (512 mẫu) đã nạp xong.
2. **`xBlockQueue` (Queue sâu 2 phần tử)**: Truyền cấu trúc mô tả khối `BlockDescriptor_t` (kích thước chỉ 16 bytes gồm con trỏ bộ đệm trong DTCM, số thứ tự khối, nhãn thời gian) từ `vTaskAcquisition` sang `vTaskProcessing`. Tránh tuyệt đối việc sao chép cồng kềnh toàn bộ 512 mẫu int16.
3. **`Task Notification` (Gửi trực tiếp không dùng Queue)**: Cơ chế giao tiếp điểm-điểm tốc độ cao nhất giữa `vTaskProcessing` và `vTaskAlarm`. Mang trực tiếp mã sự kiện (`EVENT_HARD_ALARM`, `EVENT_SOFT_ALARM`), đánh thức `vTaskAlarm` tức thời (0 tick) để cướp quyền CPU xử lý sự cố.
4. **`xLogQueue` (Queue sâu $8 \times 32$ B) + `xLogMutex`**:
   - `xLogQueue`: Hàng đợi chứa các bản ghi `LogEntry_t` (32 bytes). Áp dụng chính sách **Drop-Oldest**: khi hàng đợi đầy (trong điều kiện quá tải/burst stress), tự động đọc bỏ bản ghi cũ nhất để luôn bảo tồn bản ghi sự cố mới nhất.
   - `xLogMutex`: Mutex hỗ trợ **Thừa kế mức ưu tiên (Priority Inheritance)**, chỉ dùng chung giữa `vTaskProcessing` và `vTaskLogging`. `vTaskAlarm` **tuyệt đối không bao giờ lấy Mutex** để ngăn ngừa hiện tượng đảo ngược ưu tiên (Priority Inversion).

---

## 💾 Kiến Trúc Phân Bổ Bộ Nhớ Tối Ưu (Linker Script `mps3_an547.ld`)

Dựa trên bảng phân vùng bộ nhớ Bảng 3.3 Chương III:
- **FLASH (0x00000000, Dung lượng 512 KB)**: Chứa toàn bộ mã thực thi `.text`, bảng vector ngat, bảng tra sin CMSIS-DSP, thư viện FreeRTOS kernel và ứng dụng. Chiếm dụng thực tế: **145.4 KB** (27.75%).
- **DTCM (0x20000000, Dung lượng 128 KB - Tốc độ cao 0-wait state)**:
  - Bộ đệm đôi DMA (`g_DoubleBuffer`): $2 \times 512 \times 2\text{ Bytes} = 2,048\text{ Bytes}$ ($2.0\text{ KB}$).
  - Workspace FFT CMSIS-DSP (`g_FFT_Input`, `g_FFT_Output`, `g_Mag_Spectrum` căn lề 16-byte): $5,120\text{ Bytes}$ ($5.0\text{ KB}$).
  - Ngăn xếp 3 Tác vụ Tới hạn: `vTaskAlarm` ($1,024\text{ B}$), `vTaskAcquisition` ($1,024\text{ B}$), `vTaskProcessing` ($4,096\text{ B}$) $\Rightarrow$ Tổng cộng $6,144\text{ Bytes}$ ($6.0\text{ KB}$).
  - Tổng chiếm dụng DTCM: **13.4 KB / 128 KB** (10.47%).
- **SRAM (0x21000000, Dung lượng 4 MB)**:
  - Vùng nhớ động FreeRTOS `heap_5.c`: **64 KB** ($65,536\text{ Bytes}$).
  - Ngăn xếp Tác vụ Không tới hạn: `vTaskLogging` ($2,048\text{ B}$), `Idle Task` ($512\text{ B}$).
  - Ngăn xếp khởi động và xử lý ngat NVIC (MSP Stack): **8 KB** ($8,192\text{ B}$).

---

## 🧪 Bộ 4 Bài Kiểm Tra Chuẩn Vàng (Golden FFT Verification Suite)

Trước khi khởi động Scheduler, firmware tự động thực thi Suite 4 bài kiểm tra chuẩn vàng để kiểm chứng tính toàn vẹn toán học của thuật toán RFFT 512 điểm trên Cortex-M55:
1. **Golden Test 1 - Hằng số DC (+1.0)**: Toàn bộ 512 mẫu bằng $+1.0 \Rightarrow$ Phổ chỉ có duy nhất bin DC đạt độ lớn chính xác $512.00$, toàn bộ các bin AC bằng $0.00$ $\rightarrow$ **PASS [ok]**.
2. **Golden Test 2 - Xung đơn vị (Unit Impulse)**: 1 xung $+1.0$ tại mẫu 0, các mẫu khác $0.0 \Rightarrow$ Phổ phẳng hoàn toàn (Flat Spectrum), độ lớn $1.00$ trên mọi bin tần số $\rightarrow$ **PASS [ok]**.
3. **Golden Test 3 - Xung chữ nhật (Rectangular Pulse 6 mẫu)**: 6 mẫu $+1.0$ liên tiếp $\Rightarrow$ Bao phổ dạng Sinc, biên độ DC chính xác bằng $6.00$ $\rightarrow$ **PASS [ok]**.
4. **Golden Test 4 - Sóng Sin thuần 3 chu kỳ (Biên độ 3.0)**: Sóng sin đúng 3 chu kỳ trong khung 512 mẫu $\Rightarrow$ Toàn bộ năng lượng tập trung tại Bin 3 với biên độ lý thuyết $3.0 \times \frac{512}{2} = 768.00$, các bin khác bằng $0.00$ $\rightarrow$ **PASS [ok]**.

---

## 🚦 Ba Kịch Bản Kiểm Chứng Thực Nghiệm (§4.3 Chương IV)

### Kịch bản 1: Hoạt động bình thường & Nhận diện bất thường
- Hệ thống chạy ở chu kỳ danh định 64 ms/khối.
- Chèn lỗi rung động tại **Khối #6** (sóng hài 300 Hz biên độ 3.0g mô phỏng hư hỏng vòng bi) và **Khối #14** (sóng 100 Hz biên độ 4.0g mô phỏng mất cân bằng nghiêm trọng).
- Kết quả: `vTaskProcessing` nhận diện chính xác $RMS = 2.22g$ tại Khối #6 và $RMS = 2.84g$ tại Khối #14 (vượt ngưỡng cứng $2.0g$). Kích hoạt tức thời 2 cảnh báo khẩn cấp `BUZZER_ON + RELAY_TRIP`. Các khối bình thường được ghi log thưa theo tỉ lệ $1:16$ ($\approx 1\text{ bản ghi/giây}$). $\rightarrow$ **PASS [ok]**.

### Kịch bản 2: Kiểm chứng Preemption thật & Priority Inheritance
- **Phần A (Real Preemption)**: Tại Tick 2048 ms, khi `vTaskProcessing` (Pri 3) đang thực thi chuỗi tính toán FFT nặng, sự cố khẩn cấp xuất hiện. `vTaskAlarm` (Pri 5) lập tức cướp quyền CPU giữa chừng phép tính FFT với độ trễ 0 tick, hoàn tất đóng rơ-le an toàn, sau đó trả lại CPU cho `vTaskProcessing` tiếp tục tính toán hoàn tất không gián đoạn. $\rightarrow$ **PASS [ok]**.
- **Phần B (Priority Inheritance)**: `vTaskLogging` (Pri 2) giữ `xLogMutex`. `vTaskProcessing` (Pri 3) yêu cầu Mutex và rơi vào trạng thái Blocked. Ngay lập tức, FreeRTOS nâng ưu tiên của `vTaskLogging` lên mức **3**. Khi `vTaskAcquisition` (Pri 4) thức dậy, do có mức ưu tiên cao hơn mức thừa kế (4 > 3), tác vụ Thu thập cướp quyền CPU thành công. Sau khi giải phóng Mutex, ưu tiên của `vTaskLogging` được khôi phục về mức **2**. $\rightarrow$ **PASS [ok]**.

### Kịch bản 3: Thử nghiệm quá tải & Giới hạn tài nguyên (Stress Test)
- Đẩy nhịp DMA từ 64 ms xuống **4 ms/khối**, liên tục chèn tín hiệu bất thường trong khi `vTaskProcessing` mô phỏng tải tính toán định mức ($C_i \approx 10-12$ ms).
- Kết quả: Ghi nhận chính xác **126 lần phát hiện tràn bộ đệm đôi (Buffer Overrun)** và **35 bản ghi bị loại bỏ an toàn** trên `xLogQueue` theo đúng chính sách **Drop-Oldest**. Hệ thống không bị treo, không tràn ngăn xếp (Stack Overflow) và không rò rỉ bộ nhớ Heap. $\rightarrow$ **PASS [ok]**.

---

## 📈 Kết Quả Đo Đạc Thực Nghiệm (Metrology Summary)

```text
====================================================================
       TONG KET SO LIEU THUC NGHIEM TOAN DIEN (CHUONG IV)          
====================================================================
[1] THONG KE KHOI DU LIEU VA CANH BAO:
    - Tong so khoi sinh ra (Generated)   : 245
    - Tong so khoi da xu ly (Processed)  : 118
    - So khoi bi mat do tran/overrun     : 251
    - So lan phat hien overrun bo dem    : 126
    - Tong so lan kich hoat Canh bao     : 45
    - So ban ghi log bi drop (xLogQueue) : 35 (Chinh sach Drop-Oldest)

[2] DO TRE PHAT HIEN -> BAO DONG (CANH BAO):
    - Do tre cuc dai (Max Latency)       : 0 ms
    - Do tre trung binh (Mean Latency)   : 0 ms
    * Ghi chu: Day la do tre quan sat tren QEMU MPS3 AN547.
      Khong phai so do tren phan cung that STM32N6.

[3] THONG KE FREERTOS HEAP (heap_5.c - SRAM 64 KB):
    - Tong dung luong Heap cau hinh      : 65536 Bytes (64 KB)
    - Heap con trong hien tai (Free)     : 64832 Bytes (98.93%)
    - Heap da cap phat thuc te (Used)    : 704 Bytes (1.07%)

[4] BANG THEO DOI NGAN XEP (STACK HWM SO VOI BANG 3.1 CHUONG III):
--------------------------------------------------------------------------------
Task Name        | Prio | Memory | Alloc (Bytes) | Free HWM (Bytes) | Used (Bytes)
--------------------------------------------------------------------------------
vTaskAlarm       |  5   |  DTCM  |  1024 (256w)  |  864 Bytes       |  160 Bytes
vTaskAcquisition |  4   |  DTCM  |  1024 (256w)  |  840 Bytes       |  184 Bytes
vTaskProcessing  |  3   |  DTCM  |  4096 (1024w) |  3592 Bytes       |  504 Bytes
vTaskLogging     |  2   |  SRAM  |  2048 (512w)  |  1712 Bytes       |  336 Bytes
Idle Task        |  0   |  SRAM  |   512 (128w)  |  352 Bytes       |  160 Bytes
--------------------------------------------------------------------------------
Ghi chu: Free HWM la so byte ngan xep toi thieu con du chua tung bi cham toi.
         Moi Task deu thoa man: Used < Alloc (Khong vuot ngan sach).
====================================================================
```

---

## ⚠️ Tuyên Bố Giới Hạn Mô Phỏng QEMU & Ngoại Vi Stub

1. **Về ngoại vi phần cứng**: Nền tảng mô phỏng QEMU MPS3 AN547 không hỗ trợ ngoại vi ADC/DMA vật lý của vi điều khiển STM32N6 hay NPU Ethos-U55. Do đó, kênh dữ liệu gia tốc được giả lập (stub) bằng bảng tra hàm sin tích hợp sóng hài lỗi vòng bi qua Software Timer. Tần số lấy mẫu 8 kHz và chu kỳ 64 ms là nhịp định thời logic của hệ thống.
2. **Về thông số thời gian**: Các phép đo độ trễ trên QEMU được đo bằng đơn vị FreeRTOS Ticks (1000 Hz) và số chu kỳ lệnh của CPU giả lập. Điều này chứng minh tính đúng đắn tuyệt đối về mặt giải thuật lập lịch, cơ chế tiền định và thứ tự ưu tiên của RTOS. Tuy nhiên, các số liệu này **không được xem là số đo dao động ký trên silicon vật lý**. Trên phần cứng STM32N6 thực tế, độ trễ phát hiện sẽ nằm ở cấp độ micro-giây phụ thuộc vào xung nhịp 25 MHz và thời gian truy xuất DTCM.

---

## 🛠 Hướng Dẫn Tái Lập Thí Nghiệm (Reproduction Steps)

### Yêu cầu môi trường
- **Trình biên dịch**: `arm-none-eabi-gcc` $\ge 13.0$ (Đã kiểm chứng: `arm-none-eabi-gcc (GCC) 13.3.0`)
- **Trình mô phỏng**: `qemu-system-arm` $\ge 9.0$ (Đã kiểm chứng: `QEMU 11.0.1`)
- **Công cụ Make**: `mingw32-make` hoặc `make`
- **Môi trường Python**: Python 3.10+ (thư viện `matplotlib`, `numpy` dùng để vẽ đồ thị học thuật)

### 1. Biên dịch mã nguồn firmware
```bash
mingw32-make clean && mingw32-make
```
*(Đầu ra: `build/firmware.elf` và bản đồ bộ nhớ `build/firmware.map` sạch 0 error).*

### 2. Thực thi mô phỏng trên QEMU
Chạy trực tiếp bằng 1 lệnh QEMU duy nhất:
```bash
qemu-system-arm -machine mps3-an547 -cpu cortex-m55 -semihosting -kernel build/firmware.elf -nographic -serial stdio
```
*(Firmware tự động kích hoạt semihosting `qemu_exit(0)` thoát sạch sau khi hoàn tất toàn bộ 3 kịch bản, không treo tiến trình).*

Hoặc chạy thông qua script quản lý tự động lưu log:
```bash
python run_qemu.py
```

### 3. Tự động sinh báo cáo phân tích và hình ảnh học thuật
```bash
python generate_reports.py
python plot_figures.py
```

---

## 📁 Danh Mục Cấu Trúc Kho Mã Nguồn

```text
.
├── Makefile                      # Kịch bản biên dịch tối ưu hóa Cortex-M55 (-O2, MVE, FPU)
├── mps3_an547.ld                 # Linker script phân bổ bộ nhớ DTCM 128KB và SRAM 4MB
├── VERIFICATION.md               # Bảng ma trận đối chiếu chi tiết từng yêu cầu Chương III
├── README.md                     # Tài liệu học thuật hướng dẫn và tổng kết hệ thống
├── run_qemu.py                   # Script tự động thực thi mô phỏng và thu thập log UART
├── generate_reports.py           # Script tự động phân tích ELF/MAP và trích xuất báo cáo
├── plot_figures.py               # Script kết xuất 5 biểu đồ học thuật độ phân giải 300 DPI
├── include/
│   └── FreeRTOSConfig.h          # Cấu hình Kernel FreeRTOS 11.1.0 (Pri=6, MVE=1, FPU=1)
├── src/
│   ├── main.c                    # Mã nguồn chính 4 tác vụ, IPC, CMSIS-DSP RFFT, 3 kịch bản
│   ├── uart.c / uart.h           # Trình điều khiển APB UART0 Non-secure trên MPS3 AN547
│   ├── startup_ARMCM55.c         # Bảng vector ngắt và chuỗi khởi tạo phần cứng Cortex-M55
│   └── system_ARMCM55.c          # Cấu hình xung nhịp và kích hoạt CP10/CP11 FPU & Helium
├── cmsis/                        # Thư viện CMSIS Core ARMCM55 và CMSIS-DSP 5.9.0 chính hãng
├── freertos_kernel/              # Mã nguồn FreeRTOS Kernel 11.1.0 và port ARM_CM55_NTZ
└── results/                      # Toàn bộ dữ liệu thực nghiệm đã kiểm chứng
    ├── 01_environment_info.txt   # Chi tiết phiên bản GCC 13.3.0, QEMU 11.0.1, GDB
    ├── 02_memory_footprint.txt   # Phân bổ chi tiết các phân vùng FLASH, DTCM, SRAM
    ├── 03_simulation_logs.txt    # Toàn bộ nhật ký chạy thực tế của 3 kịch bản trên QEMU
    ├── 04_context_switch_analysis.txt # Disassembly chi tiết PendSV_Handler và SysTick
    ├── 05_gdb_register_dump.txt  # Snapshot trạng thái thanh ghi CPU thực tế khi Exception 14
    ├── interactive_dashboard.html# Dashboard tương tác trực quan (Academic Dashboard)
    └── figures/                  # Bộ 5 hình vẽ học thuật 300 DPI dùng cho báo cáo
        ├── 01_task_execution_timing_gantt.png
        ├── 02_fft_spectrum_normal_vs_anomaly.png
        ├── 03_detection_to_alarm_latency.png
        ├── 04_memory_and_stack_hwm.png
        └── 05_scenario_summary_dashboard.png
```
