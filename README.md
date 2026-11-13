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
| **`vTaskAlarm`** | **5** (Cao nhất) | Phi chu kỳ ($D_i = 10.0$ ms) | $1.0$ ms | **DTCM** | 1,024 B (256 words) | Chờ Task Notification mang mã sự kiện từ Xử lý. Kích hoạt trực tiếp cơ cấu chấp hành (Còi/Đèn/Rơ-le) qua thanh ghi; được phép in trực tiếp 1 dòng thông báo khẩn cấp ra UART (vì không thể chờ Logging chu kỳ 1,000 ms), tuyệt đối không lấy Mutex hay gửi qua queue thường. |
| **`vTaskAcquisition`** | **4** | Chu kỳ $T = 64$ ms ($D_i = 64$ ms) | $2.0$ ms | **DTCM** | 1,024 B (256 words) | Chờ Binary Semaphore `xBlockReady` từ ngắt DMA stub. Đóng gói mô tả khối (`BlockDescriptor_t`) gửi vào `xBlockQueue`. |
| **`vTaskProcessing`** | **3** | Chu kỳ $T = 64$ ms ($D_i = 64$ ms) | $12.0$ ms | **DTCM** | 4,096 B (1,024 words) | Nhận khối, tăng tốc Vector Helium CMSIS-DSP tính toán FFT 512 mẫu, trích xuất đặc trưng rung động, so ngưỡng quyết định phát tín hiệu cảnh báo. |
| **`vTaskLogging`** | **2** | Chu kỳ $T = 1,000$ ms ($D_i = 1,000$ ms) | $20.0$ ms | **SRAM** | 2,048 B (512 words) | Nhận bản ghi telemetry qua `xLogQueue` (bảo vệ bởi `xLogMutex`) và `xAlarmLogQueue`. Quản lý in log định kỳ ra UART và xả hàng đợi. (Lưu ý: Việc điều phối chuyển trạng thái 3 kịch bản kiểm thử được tách biệt hoàn toàn sang `vScenarioTimerCallback`). |
| **`Idle Task`** | **0** (Thấp nhất) | Khi CPU rảnh rỗi | - | **SRAM** | 512 B (128 words) | Thu dọn tác vụ đã xóa, hỗ trợ `configUSE_TICKLESS_IDLE = 1` để tối ưu năng lượng. |

> **Kiểm chứng khả năng lập lịch (RMA Schedulability)**:  
> Hệ thống có đúng $n = 3$ tác vụ định kỳ độc lập (`vTaskAlarm` là tác vụ phi chu kỳ / sporadic hướng sự kiện nên không tính vào $n$). Tổng hệ số sử dụng CPU:
> $$U = \frac{C_{\text{Acq}}}{T_{\text{Acq}}} + \frac{C_{\text{Proc}}}{T_{\text{Proc}}} + \frac{C_{\text{Log}}}{T_{\text{Log}}} = \frac{2.0}{64.0} + \frac{12.0}{64.0} + \frac{20.0}{1000.0} = 3.125\% + 18.75\% + 2.0\% = 23.875\% \approx 23.9\%$$
> Giá trị này thấp hơn nhiều so với cận trên Liu & Layland cho $n = 3$: $U_{\text{bound}}(3) = 3(2^{1/3} - 1) \approx 78.0\%$. *(Lưu ý: Cận 75.7% xuất hiện khi đếm nhầm 4 tác vụ chu kỳ; thiết kế Chương III chuẩn xác là $n = 3$ với ngưỡng $78.0\%$)*. Toàn bộ các tác vụ đảm bảo đáp ứng hạn định nghiêm ngặt!

---

## 🔄 4 Đối Tượng Đồng Bộ & Giao Tiếp Liên Tác Vụ (IPC)

1. **`xBlockReady` (Binary Semaphore)**: Cầu nối trực tiếp giữa Trình phục vụ ngắt DMA stub (`Interrupt0_Handler`) và `vTaskAcquisition` khi một nửa bộ đệm đôi (512 mẫu) đã nạp xong.
2. **`xBlockQueue` (Queue sâu 2 phần tử)**: Truyền cấu trúc mô tả khối `BlockDescriptor_t` (kích thước chỉ 16 bytes gồm con trỏ bộ đệm trong DTCM, số thứ tự khối, nhãn thời gian) từ `vTaskAcquisition` sang `vTaskProcessing`. Tránh tuyệt đối việc sao chép cồng kềnh toàn bộ 512 mẫu int16.
3. **`Task Notification` (Gửi trực tiếp không dùng Queue)**: Cơ chế giao tiếp điểm-điểm tốc độ cao nhất giữa `vTaskProcessing` (hoặc ISR ngắt khẩn cấp) và `vTaskAlarm`. Mang trực tiếp mã sự kiện (`EVENT_HARD_ALARM`, `EVENT_SOFT_ALARM`), đánh thức `vTaskAlarm` tức thời để cướp quyền CPU xử lý sự cố.
4. **Phân tách Hàng đợi Log Thường & Log Cảnh báo**:
   - `xLogQueue` (Queue sâu $8 \times 32$ B): Hàng đợi chứa các bản ghi `LogEntry_t` telemetry định kỳ. Áp dụng chính sách **Drop-Oldest**: khi hàng đợi đầy trong điều kiện quá tải/burst stress, tự động đọc bỏ bản ghi cũ nhất để luôn bảo tồn bản ghi mới nhất. `vTaskProcessing` gửi vào hàng đợi theo cơ chế non-blocking (`timeout = 0`) để loại trừ nguy cơ đảo ngược ưu tiên ngược (Reverse Priority Inversion).
   - `xAlarmLogQueue` (Queue sâu $8 \times 32$ B): Hàng đợi riêng biệt dành độc quyền cho bản ghi sự cố cảnh báo khẩn cấp, bảo toàn nguyên vẹn trong thực nghiệm: 0 bản ghi cảnh báo mất trong lần chạy này (3 cảnh báo / queue sâu 8, xả mỗi 1.000 ms).
   - `xLogMutex`: Mutex hỗ trợ **Thừa kế mức ưu tiên (Priority Inheritance)**, chỉ dùng chung giữa `vTaskProcessing` và `vTaskLogging`. `vTaskAlarm` **tuyệt đối không bao giờ lấy Mutex** để ngăn ngừa hiện tượng đảo ngược ưu tiên (Priority Inversion).

---

## 💾 Kiến Trúc Phân Bổ Bộ Nhớ Tối Ưu (Linker Script `mps3_an547.ld`)

Dựa trên bảng phân vùng bộ nhớ Bảng 3.3 Chương III:
- **FLASH (0x00000000, Dung lượng 512 KB)**: Chứa toàn bộ mã thực thi `.text`, bảng vector ngắt, bảng tra sin CMSIS-DSP, thư viện FreeRTOS kernel và ứng dụng. Chiếm dụng thực tế: **145.07 KB** (28.33%).
- **DTCM (0x20000000, Dung lượng 128 KB - Tốc độ cao 0-wait state)**:
  - Bộ đệm đôi DMA (`g_DoubleBuffer`): $2 \times 512 \times 2\text{ Bytes} = 2,048\text{ Bytes}$ ($2.0\text{ KB}$).
  - Workspace FFT CMSIS-DSP (`g_FFT_Input`, `g_FFT_Output` căn lề 16-byte): **đúng 4,096 Bytes** ($4.0\text{ KB}$, Bảng 3.3 Chương III, tái sử dụng `g_FFT_Input` cho phổ biên độ, không cấp phát mảng 1.024 B dư thừa).
  - Ngăn xếp 3 Tác vụ Tới hạn: `vTaskAlarm` ($1,024\text{ B}$), `vTaskAcquisition` ($1,024\text{ B}$), `vTaskProcessing` ($4,096\text{ B}$) $\Rightarrow$ Tổng cộng $6,144\text{ Bytes}$ ($6.0\text{ KB}$).
  - Tổng chiếm dụng DTCM: **12.4 KB / 128 KB** (9.69%).
- **SRAM (0x21000000, Dung lượng 4 MB)**:
  - Vùng nhớ động FreeRTOS `heap_5.c`: **64 KB** ($65,536\text{ Bytes}$).
  - Ngăn xếp Tác vụ Không tới hạn: `vTaskLogging` ($2,048\text{ B}$), `Idle Task` ($512\text{ B}$).
  - Ngăn xếp khởi động và xử lý ngắt NVIC (MSP Stack): **8 KB** ($8,192\text{ B}$).
  - Tổng SRAM chiếm dụng: **136.18 KB / 4,096 KB** (3.32%).

---

## 🧪 Bộ 4 Bài Kiểm Tra Chuẩn Vàng (Golden FFT Verification Suite)

Trước khi khởi động Scheduler, firmware tự động thực thi Suite 4 bài kiểm tra chuẩn vàng (kiểm soát bởi cổng điều kiện `if (all_passed)`) để kiểm chứng tính toàn vẹn toán học của thuật toán RFFT 512 điểm trên Cortex-M55:
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
- **Phần A (Real Mid-FFT Preemption)**: Khi `vTaskProcessing` (Pri 3) đang thực thi biến đổi Fourier, ngắt phần mềm `Interrupt1_IRQn` được kích hoạt từ hook `rfft_mid_stage_hook()` ngay giữa các tầng bướm tính toán FFT. `vTaskAlarm` (Pri 5) lập tức cướp quyền CPU giữa chừng phép tính FFT, xác thực bất đẳng thức thời gian 4 điểm:
  `[Preemption] Preemption verified! [PASS] [ok] t_start=51207900 < t_irq=51215740 <= t_actuate=51224313 < t_end=51224998 (cycles)`.
  Sau khi hoàn tất đóng rơ-le an toàn, quyền điều khiển được trả lại cho `vTaskProcessing` tiếp tục tính toán hoàn tất không lỗi. $\rightarrow$ **PASS [ok]**.
- **Phần B (Genuine Priority Inheritance)**: Tranh chấp 3 task thật trên `xLogMutex` (§3.3.3 Chương III). `vTaskLogging` (Pri 2) giữ `xLogMutex`. `vTaskProcessing` (Pri 3) yêu cầu Mutex và bị chặn. FreeRTOS kernel tự động nâng ưu tiên của `vTaskLogging` từ 2 lên **3**. Khi `vTaskAcquisition` (Pri 4) thức dậy, do có ưu tiên cao hơn (4 > 3), tác vụ Thu thập cướp CPU thành công (`Step 4: Acquisition arrives -> Preempts boosted Logging`). Sau khi giải phóng Mutex, ưu tiên của `vTaskLogging` tự động khôi phục về mức gốc **2**. Cấm tuyệt đối `vTaskPrioritySet` để mô phỏng.
  *Ghi chú kiểm thử xác định*: Việc dừng timer DMA và kích hoạt ngắt `Interrupt0_IRQn` bằng tay qua `NVIC_SetPendingIRQ()` trong demo là kỹ thuật chuẩn của kiểm thử RTOS xác định (Deterministic Testing). Việc giữ Mutex xuyên suốt chuỗi demo là có chủ đích, bởi thao tác sao chép 1 bản ghi 32 B chỉ tốn ~10 chu kỳ CPU ($0.4\ \mu\text{s}$ ở 25 MHz), không đủ thời gian để task Pri 4 bất đồng bộ xuất hiện nếu không có dàn dựng đồng bộ. $\rightarrow$ **PASS [ok]**.

### Kịch bản 3: Thử nghiệm quá tải & Đẳng thức toán học (Stress Test)
- Đẩy nhịp DMA từ 64 ms xuống **4 ms/khối** ($16\times$ quá tải), mô phỏng tắc nghẽn bộ đệm đôi và bão hòa hàng đợi.
- Kết quả nghiệm thu đẳng thức bảo toàn:
  * **Đẳng thức khối**: $\text{Generated} (357) = \text{Processed} (249) + \text{Buffer\_Overruns} (108)$ $\rightarrow$ **DANG THUC DUNG [PASS] [ok]**.
  * **Đẳng thức log thường**: $\text{Normal\_Gen} (217) = \text{Normal\_Written} (19) + \text{Normal\_Dropped} (198)$ $\rightarrow$ **DANG THUC DUNG [PASS] [ok]** *(Normal Written bao gồm cả các bản ghi được xả ở giai đoạn drain)*.
  * **Bảo toàn cảnh báo**: $\text{Alarm\_Gen} = 3,\ \text{Alarm\_Written} = 3,\ \mathbf{Alarm\_Dropped = 0}$ $\rightarrow$ **0 BAN GHI CANH BAO MAT TRONG LAN CHAY NAY (3 canh bao / queue sau 8, xa moi 1000 ms) [PASS] [ok]**.

---

## 📈 Kết Quả Đo Đạc Thực Nghiệm (Metrology Summary)

```text
====================================================================
       TONG KET SO LIEU THUC NGHIEM TOAN DIEN (CHUONG IV - V4)     
====================================================================
[1] THONG KE KHOI DU LIEU VA LOG (DANG THUC TOAN HOC):
    - Tong so khoi sinh ra (Generated)   : 357
    - Tong so khoi da xu ly (Processed)  : 249
    - So lan tran bo dem DMA (Overrun)   : 108
    * Kiem tra dang thuc khoi: Generated == Processed + Overrun
      (357 == 357) -> DANG THUC DUNG [PASS] [ok]

[2] PHAN TACH HANG DOI LOG THUONG VA LOG CANH BAO:
    - Log thuong sinh ra (Normal Gen)    : 217
    - Log thuong da ghi (Normal Written) : 19 (Bao gom ban ghi xa o giai doan drain)
    - Log thuong bi drop (Normal Drop)   : 198 (Chinh sach Drop-Oldest tren xLogQueue)
    * Kiem tra dang thuc log thuong: Generated == Written + Dropped
      (217 == 217) -> DANG THUC DUNG [PASS] [ok]
    - Log canh bao sinh (Alarm Gen)      : 3
    - Log canh bao da ghi (Alarm Written): 3
    - Log canh bao bi drop (Alarm Drop)  : 0 -> 0 BAN GHI CANH BAO MAT TRONG LAN CHAY NAY (3 canh bao / queue sau 8, xa moi 1000 ms) [PASS] [ok]

[3] DO TRE PHAT HIEN -> BAO DONG (DO CHU KY SYSTICK):
    - Do tre cuc dai (Max Latency)       : 8573 cycles (342.92 us)
    - Do tre trung binh (Mean Latency)   : 3032.25 cycles (121.29 us)
    * Ghi chu học thuật: So lieu do bang SysTick tren QEMU MPS3 AN547 (25 MHz).
      Khong phai so do dao dong ky tren phan cung that STM32N6.

[4] THONG KE FREERTOS HEAP (heap_5.c - SRAM 64 KB):
    - Tong dung luong Heap cau hinh      : 65536 Bytes (64 KB)
    - Heap con trong hien tai (Free)     : 64352 Bytes (98.19%)
    - Heap da cap phat thuc te (Used)    : 1184 Bytes (1.81%)

[5] BANG THEO DOI NGAN XEP (STACK HWM SO VOI BANG 3.1 CHUONG III):
--------------------------------------------------------------------------------
Task Name        | Prio | Memory | Alloc (Bytes) | Free HWM (Bytes) | Used (Bytes)
--------------------------------------------------------------------------------
vTaskAlarm       |  5   |  DTCM  |  1024 (256w)  |  696 Bytes       |  328 Bytes
vTaskAcquisition |  4   |  DTCM  |  1024 (256w)  |  800 Bytes       |  224 Bytes
vTaskProcessing  |  3   |  DTCM  |  4096 (1024w) |  3536 Bytes       |  560 Bytes
vTaskLogging     |  2   |  SRAM  |  2048 (512w)  |  1680 Bytes       |  368 Bytes
Idle Task        |  0   |  SRAM  |   512 (128w)  |  352 Bytes       |  160 Bytes
--------------------------------------------------------------------------------
Ngan sach ly thuyet vTaskProcessing (Paper Budget): ~616 Bytes
  (Context: 212 B + Locals: 120 B + FFT Call: 96 B + Unpack: 64 B + API: 80 B + Margin: 44 B)
  -> So voi cap phat 4096 B: He so an toan 7.3x (Thuc te HWM su dung: 560 Bytes / 13.7%)
--------------------------------------------------------------------------------
Ghi chu: Free HWM la so byte ngan xep toi thieu con du chua tung bi cham toi.
Ket luan: [PASS] [ok] Moi Task deu thoa man: Used < Alloc (Khong vuot ngan sach).

[6] DOI CHIEU THOI GIAN DAP UNG RTA: THIET KE VS THUC DO (§3.3.3 VS §4.4.4):
----------------------------------------------------------------------------------------------------
Task Name        | Prio | Design Ci | Meas Max Ci | Design RTA | Meas RTA   | Deadline  | Slack Margin | Status
-----------------|------|-----------|-------------|------------|------------|-----------|--------------|--------
vTaskAlarm       |  5   | 1.00 ms   |   0.558 ms |  <=1.0 ms  |   0.558 ms | 10.00 ms  |   9.442 ms | [PASS] [ok]
vTaskAcquisition |  4   | 2.00 ms   |   0.073 ms |  2.00 ms   |   0.631 ms | 64.00 ms  |   63.369 ms | [PASS] [ok]
vTaskProcessing  |  3   | 12.00 ms  |   6.983 ms |  15.00 ms  |   7.714 ms | 64.00 ms  |   56.286 ms | [PASS] [ok]
vTaskLogging     |  2   | 20.00 ms  |   3.000 ms |  35.00 ms  |   10.614 ms | 1000.0ms  |   989.386 ms | [PASS] [ok]
----------------------------------------------------------------------------------------------------
Ghi chu: Gia thiet RTA: B_3 <= 0.100 ms (sao chep ban ghi 32 byte), B_4 = 0 ms (uu tien thap nhat).
         Toan bo 4 tac vu deu dat bien an toan (Slack Margin) > 87%, khong co deadline miss.
====================================================================
```

---

## 📡 Quy Tắc Truyền Thông UART & Các Điểm In Trực Tiếp

Quy tắc chuẩn hóa trong thiết kế Chương III: **Toàn bộ nhật ký vận hành (Operational Telemetry) của 4 tác vụ ứng dụng bắt buộc phải gửi qua `xLogQueue` hoặc `xAlarmLogQueue` để `vTaskLogging` in ra UART**. Ngoại lệ vận hành duy nhất là dòng thông báo khẩn cấp của `vTaskAlarm` (Pri 5), được phép in trực tiếp ra phần cứng UART nhằm đảm bảo đáp ứng hạn ngặt $D_i = 10.0\text{ ms}$ mà không thể chờ chu kỳ 1.000 ms của `vTaskLogging`.

Ngoài ngoại lệ vận hành trên, mọi điểm in UART trực tiếp (`uart_puts`) còn lại trong mã nguồn đều thuộc về hạ tầng kiểm thử và chẩn đoán hệ thống:
1. **Khởi tạo hệ thống trước Scheduler (`main`, `init_dtcm_memory`)**: In banner và cấu hình heap khi chưa có task nào chạy.
2. **Bộ kiểm tra chuẩn vàng (`run_golden_fft_tests`)**: In kết quả xác minh 4 test FFT trước khi khởi động Scheduler.
3. **Các Hook xử lý lỗi nghiêm trọng (`vApplicationStackOverflowHook`, `vApplicationMallocFailedHook`, `vAssertCalled`)**: Dừng khẩn cấp khi tràn stack hoặc cạn heap.
4. **Bộ điều phối kịch bản (`vScenarioTimerCallback`)**: In tiêu đề chuyển đổi trạng thái kịch bản và kết quả kiểm thử `[PASS] [ok]`.
5. **Điểm xác nhận Preemption giữa FFT (`vTaskProcessing`)**: In 1 dòng chứa dấu vết 4 timestamp chứng minh $t_{\text{start}} < t_{\text{irq}} \le t_{\text{actuate}} < t_{\text{end}}$.
6. **Điểm xác nhận cướp quyền trong Kịch bản 2B (`vTaskAcquisition`)**: In dòng xác nhận Thu thập (Pri 4) cướp CPU từ Logging (Pri 3 kế thừa).
7. **Báo cáo tổng kết thực nghiệm cuối cùng (`print_final_benchmark_report`)**: In bảng HWM và phân tích bộ nhớ sau khi dừng thu thập.

---

## 🔔 Giải Thích: 12 Lần Kích Hoạt Cảnh Báo vs 3 Bản Ghi Nhật Ký

Trong quá trình thực nghiệm, có thể ghi nhận tổng cộng 12 lần cơ cấu cảnh báo phần cứng được kích hoạt, trong khi chỉ có 3 bản ghi nhật ký cảnh báo được lưu vào `xAlarmLogQueue`:
- **Đường xử lý khẩn cấp (Fast Path)**: Mỗi khi có ngắt khẩn cấp (bao gồm cả các probe ngắt `Interrupt1_IRQn` lặp lại giữa phép FFT trong Kịch bản 2 để đo đạc thời gian đáp ứng), `vTaskAlarm` nhận tín hiệu Task Notification và kích hoạt ngay lập tức cơ cấu chấp hành phần cứng (còi/đèn/rơ-le) mà không tạo bản ghi log.
- **Đường lưu trữ nhật ký (Telemetry Path)**: Chỉ khi `vTaskProcessing` phát hiện sự cố rung động thực sự qua phân tích phổ FFT (Khối #6, Khối #14 và khối bất thường Kịch bản 3), bản ghi `LogEntry_t` (RMS, Peak, CF, Freq) mới được đóng gói gửi vào `xAlarmLogQueue`.
- Sự phân tách này đảm bảo cơ cấu an toàn phản ứng tức thì mà không bị nghẽn bởi việc định dạng chuỗi dữ liệu.

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
├── plot_figures.py               # Script kết xuất 8 biểu đồ học thuật độ phân giải 300 DPI
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
    ├── 06_measured_rta_and_runtime_stats.txt # Báo cáo đo lường RTA thực tế và thống kê Run-Time
    ├── interactive_dashboard.html# Dashboard tương tác trực quan (Academic Dashboard V4)
    └── figures/                  # Bộ 8 hình vẽ học thuật 300 DPI dùng cho báo cáo
        ├── 01_task_execution_timing_gantt.png            # Hình 4.1: Gantt định thời lý thuyết (§3.1)
        ├── 02_fft_spectrum_normal_vs_anomaly.png         # Hình 4.2: Phổ FFT bình thường vs lỗi
        ├── 03_detection_to_alarm_latency.png             # Hình 4.3: Timeline độ trễ phát hiện đến báo động
        ├── 04_memory_and_stack_hwm.png                   # Hình 4.4: Phân bổ bộ nhớ & Stack HWM
        ├── 05_scenario_summary_dashboard.png             # Hình 4.5: Dashboard tổng kết 3 kịch bản
        ├── 06_task_execution_timing_gantt_measured.png   # Hình 4.6: Gantt đo thực tế từ RAM Trace ([TRACE])
        ├── 07_four_task_latency_histogram.png            # Hình 4.7: Histogram độ trễ đáp ứng 4 Task
        └── 08_rta_design_vs_measured.png                 # Hình 4.8: Đối chiếu RTA thiết kế vs thực đo
```
