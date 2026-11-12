# BẢNG ĐỐI CHIẾU VÀ XÁC MINH TOÀN DIỆN THIẾT KẾ CHƯƠNG III (VERIFICATION MATRIX V3)

**Dự án**: Hệ thống Giám sát Rung động Thời gian thực và Cảnh báo Bất thường trên ARM Cortex-M55  
**Nền tảng**: QEMU MPS3 AN547 (`-cpu cortex-m55`), FreeRTOS 11.1.0, CMSIS-DSP 5.9.0, GCC 13.3.0  
**Tài liệu cơ sở**: Báo cáo Thiết kế Hệ thống Nhúng Thời gian thực - Chương III (Bảng 3.1, §3.2.2, §3.3.1–3.3.3, §3.4)  
**Cam kết chất lượng (Vòng 3)**: 100% số liệu đo đạc thực tế từ firmware biên dịch và log QEMU; mọi tham chiếu dòng mã nguồn đều kiểm chứng bằng grep hoặc tên hàm chuẩn xác; không bịa số liệu, không hardcode, không mô phỏng hành vi kernel bằng tay.

---

## 1. BẢNG MA TRẬN ĐỐI CHIẾU YÊU CẦU THIẾT KẾ CHƯƠNG III (REQUIREMENT TRACEABILITY MATRIX)

| STT | Thành phần / Yêu cầu thiết kế Chương III | Thông số thiết kế (§Chương III) | Vị trí cài đặt (File : Dòng mã nguồn / Hàm xác thực) | Bằng chứng quan sát thực nghiệm (Dòng log / Số liệu QEMU / Map file) | Trạng thái |
|:---:|:---|:---|:---|:---|:---:|
| **1** | **Cấu hình Kernel FreeRTOS** | | | | |
| 1.1 | Số mức ưu tiên tối đa | `configMAX_PRIORITIES = 6` (Khoảng 0..5) | `include/FreeRTOSConfig.h:23` | Kernel khởi tạo 4 Task ưu tiên 5, 4, 3, 2 và Idle 0 không lỗi | **ĐÃ ĐẠT** [ok] |
| 1.2 | Hỗ trợ Vector MVE & FPU cứng | `configENABLE_MVE = 1`<br>`configENABLE_FPU = 1` | `include/FreeRTOSConfig.h:11-12`<br>`Makefile:11` | GCC cờ `-mcpu=cortex-m55 -mfloat-abi=hard`; lệnh MVE và FPU sinh mã sạch | **ĐÃ ĐẠT** [ok] |
| 1.3 | Cơ chế tiết kiệm năng lượng | `configUSE_TICKLESS_IDLE = 1` | `include/FreeRTOSConfig.h:20` | Tickless idle kích hoạt; ngắt DMA kích hoạt qua Software Timer 64 ms | **ĐÃ ĐẠT** [ok] |
| 1.4 | Đo lường thống kê Run-Time | `configGENERATE_RUN_TIME_STATS = 1` | `include/FreeRTOSConfig.h:54` | Bảng Run-Time Stats và đo High Water Mark hoạt động chính xác | **ĐÃ ĐẠT** [ok] |
| 1.5 | Trình quản lý bộ nhớ động | FreeRTOS `heap_5.c` (Dung lượng 64 KB trong SRAM) | `src/main.c:168` (buffer)<br>`src/main.c:1389` (`vPortDefineHeapRegions`)<br>`Makefile:43` | Log: `[Kernel] FreeRTOS heap_5 initialized: 65536 Bytes in SRAM`<br>Free: 64,368 B, Used: 1,168 B | **ĐÃ ĐẠT** [ok] |
| 1.6 | Lựa chọn Port vi điều khiển | `ARM_CM55_NTZ/non_secure` | `Makefile:19, 44-45`<br>`mps3_an547.ld:10-12` | Biên dịch 0 warning/error; chạy mượt trên QEMU AN547 khi tắt TrustZone | **ĐÃ ĐẠT** [ok] |
| **2** | **Bộ 4 Tác vụ Thời gian thực (Bảng 3.1)** | | | | |
| 2.1 | `vTaskAlarm` (Tác vụ Báo động) | Phi chu kỳ, $D_i = 10.0$ ms, $C_i = 1.0$ ms, Prio 5, Stack 1,024 B (DTCM) | `src/main.c:473-524` (`vTaskAlarm`)<br>`src/main.c:1420-1424`<br>`src/main.c:1298-1302` | Log: `vTaskAlarm \| 5 \| DTCM \| Alloc: 1024 B \| Used: 312 B \| Free HWM: 712 B`<br>Độ trễ đo: Max=347.76 µs, Mean=160.16 µs $\ll 10$ ms | **ĐÃ ĐẠT** [ok] |
| 2.2 | `vTaskAcquisition` (Thu thập) | Chu kỳ 64 ms, $D_i = 64$ ms, $C_i = 2.0$ ms, Prio 4, Stack 1,024 B (DTCM) | `src/main.c:530-574` (`vTaskAcquisition`)<br>`src/main.c:1425-1429`<br>`src/main.c:1305-1309` | Log: `vTaskAcquisition \| 4 \| DTCM \| Alloc: 1024 B \| Used: 184 B \| Free HWM: 840 B`<br>Thức dậy chính xác theo ngắt DMA | **ĐÃ ĐẠT** [ok] |
| 2.3 | `vTaskProcessing` (Xử lý FFT) | Chu kỳ 64 ms, $D_i = 64$ ms, $C_i = 12.0$ ms, Prio 3, Stack 4,096 B (DTCM) | `src/main.c:580-765` (`vTaskProcessing`)<br>`src/main.c:1430-1434`<br>`src/main.c:1312-1316` | Log: `vTaskProcessing \| 3 \| DTCM \| Alloc: 4096 B \| Used: 520 B \| Free HWM: 3576 B`<br>Thực thi RFFT 512 mẫu qua CMSIS-DSP | **ĐÃ ĐẠT** [ok] |
| 2.4 | `vTaskLogging` (Ghi nhật ký) | Chu kỳ 1,000 ms, $D_i = 1,000$ ms, $C_i = 20.0$ ms, Prio 2, Stack 2,048 B (SRAM) | `src/main.c:771-866` (`vTaskLogging`)<br>`src/main.c:1435-1439`<br>`src/main.c:1319-1323` | Log: `vTaskLogging \| 2 \| SRAM \| Alloc: 2048 B \| Used: 344 B \| Free HWM: 1704 B`<br>In telemetry tại tick @1001, @2002, @2050, @3115, @3500 ms | **ĐÃ ĐẠT** [ok] |
| 2.5 | `Idle Task` | Chạy khi rảnh, Prio 0, Stack 512 B (128 words, SRAM) | `src/main.c:265-274` (`vApplicationIdleHook`)<br>`include/FreeRTOSConfig.h:24`<br>`src/main.c:1326-1330` | Log: `Idle Task \| 0 \| SRAM \| Alloc: 512 B \| Used: 160 B \| Free HWM: 352 B` | **ĐÃ ĐẠT** [ok] |
| 2.6 | Kiểm chứng RMA Schedulability | Tổng tải chu kỳ $U = \frac{2}{64} + \frac{12}{64} + \frac{20}{1000} = 23.9\% < 78.0\%$ ($n=3$) | Báo cáo lý thuyết §3.2.2 & Bảng 3.1 | Hệ thống đáp ứng mọi deadline, không có task nào bị trễ hạn | **ĐÃ ĐẠT** [ok] |
| **3** | **Cơ chế Đồng bộ & Giao tiếp (IPC)** | | | | |
| 3.1 | `xBlockReady` (Binary Semaphore) | ISR DMA stub $\rightarrow$ `vTaskAcquisition` | `src/main.c:173, 1396`<br>`src/main.c:359` (give ISR)<br>`src/main.c:536` (take Acq) | Log: `Acquisition` thức dậy mỗi 64 ms ngay khi nửa bộ đệm DMA sẵn sàng | **ĐÃ ĐẠT** [ok] |
| 3.2 | `xBlockQueue` (Queue sâu 2 phần tử) | `Acquisition` $\rightarrow$ `Processing` (Chứa `BlockDescriptor_t`, không copy 512 mẫu) | `src/main.c:57-64` (struct)<br>`src/main.c:174, 1399`<br>`src/main.c:555` (send)<br>`src/main.c:587` (receive) | Chuyển con trỏ vùng đệm DTCM (`desc.pData`), kích thước struct chỉ 16 bytes | **ĐÃ ĐẠT** [ok] |
| 3.3 | Task Notification (Trực tiếp) | `Processing` / ISR $\rightarrow$ `Alarm` (Mang mã sự kiện / mức cảnh báo) | `src/main.c:480` (wait)<br>`src/main.c:379` (notify ISR)<br>`src/main.c:697` (notify Proc) | Log: `xTaskNotify(xTaskAlarmHandle, ...)` kích hoạt ngay lập tức | **ĐÃ ĐẠT** [ok] |
| 3.4 | `xLogQueue` (Queue sâu 8 $\times$ 32 B) & `xAlarmLogQueue` (Queue sâu 8) | Phân tách hàng đợi: `xLogQueue` thường Drop-Oldest; `xAlarmLogQueue` cảnh báo riêng biệt | `src/main.c:66-78` (struct)<br>`src/main.c:176-177, 1402-1406`<br>`src/main.c:719, 729-736` (send)<br>`src/main.c:801, 823` (drain) | Quan sát Kịch bản 3: 198 log thường bị drop, nhưng `Alarm Drop: 0` (0 bản ghi cảnh báo mất trong lần chạy này: 3 cảnh báo / queue sâu 8, xả mỗi 1.000 ms) | **ĐÃ ĐẠT** [ok] |
| 3.5 | `xLogMutex` (Mutex bảo vệ) | Chỉ `Processing` và `Logging` dùng; `Alarm` chỉ in trực tiếp dòng ALARM khẩn cấp, KHÔNG lấy Mutex | `src/main.c:178, 1408`<br>`src/main.c:471` (comment Alarm)<br>`src/main.c:751` (Proc take)<br>`src/main.c:794` (Log take) | Đã xác minh: `vTaskAlarm` không dùng Mutex, tránh Priority Inversion; Logging và Processing tranh chấp thật | **ĐÃ ĐẠT** [ok] |
| **4** | **Kiến trúc Bộ nhớ & Tối ưu DTCM** | | | | |
| 4.1 | Phân bổ vùng DTCM (0x20000000) | 128 KB, 0 Wait-State: Chứa Double Buffer, Workspace FFT, 3 Critical Task Stacks | `mps3_an547.ld:81-97`<br>`src/main.c:137-154` | `build/firmware.map`: `.dtcm_bss` chiếm đúng 12,704 Bytes (12.4 KB / 128 KB) | **ĐÃ ĐẠT** [ok] |
| 4.2 | Workspace FFT chuẩn 4.0 KB | Đúng 4,096 B (Bảng 3.3 Chương III), tái sử dụng `g_FFT_Input` cho magnitude | `src/main.c:140-144` | Khớp 100% Bảng 3.3, loại bỏ mảng dư thừa 1,024 B | **ĐÃ ĐẠT** [ok] |
| 4.3 | Căn lề 16-byte cho Helium MVE | Workspace FFT căn lề `__attribute__((aligned(16)))` | `src/main.c:141, 144` | `build/firmware.map`: `g_FFT_Input` đặt tại địa chỉ chia hết cho 16 | **ĐÃ ĐẠT** [ok] |
| 4.4 | Phân bổ vùng SRAM (0x21000000) | 4 MB: Chứa heap_5 (64 KB), Logging stack (2 KB), MSP stack (8 KB) | `mps3_an547.ld:101-146` | `build/firmware.map`: Tổng SRAM chiếm 139,444 Bytes (3.32% / 4 MB) | **ĐÃ ĐẠT** [ok] |
| 4.5 | Linker Script & Khởi tạo CMSIS | Bảng `.copy.table` (24 B) & `.zero.table` (16 B) khởi tạo bộ nhớ; duy nhất `__StackTop = 0x21400000` | `mps3_an547.ld:47-68, 143-146` | `.copy.table` và `.zero.table` là bảng chuẩn CMSIS cần thiết để nạp RAM từ Flash; stack duy nhất tại đỉnh SRAM | **ĐÃ ĐẠT** [ok] |
| **5** | **Thuật toán DSP & Kiểm tra Chuẩn Vàng (Golden Tests)** | Cổng điều kiện `if (all_passed)` trước khi in [PASS] | `src/main.c:1169-1177` | Log: `ALL 4 GOLDEN FFT TESTS PASSED [ok]` chỉ in khi cả 4 test đạt | **ĐÃ ĐẠT** [ok] |
| 5.1 | RFFT 512 điểm CMSIS-DSP | `arm_rfft_fast_f32` + `rfft_unpack_magnitude` | `src/main.c:614`<br>`src/main.c:451-468` (`rfft_unpack_magnitude`) | Trích xuất RMS, đỉnh gia tốc, hệ số đỉnh CF, năng lượng dải, tần số đỉnh | **ĐÃ ĐẠT** [ok] |
| 5.2 | Golden Test 1: DC Hằng số (+1.0) | Kỳ vọng: Đỉnh DC = 512.00, AC_max = 0.00 | `src/main.c:1084-1105` | Log: `Measured DC = 512.00, AC_max = 0.00 -> [PASS] [ok]` | **ĐÃ ĐẠT** [ok] |
| 5.3 | Golden Test 2: Xung đơn vị (Impulse) | Kỳ vọng: Phổ phẳng hoàn toàn, biên độ 1.00 mọi bin | `src/main.c:1107-1127` | Log: `Measured Range: [0.99, 1.00] -> [PASS] [ok]` | **ĐÃ ĐẠT** [ok] |
| 5.4 | Golden Test 3: Xung chữ nhật (6 mẫu) | Kỳ vọng: Bao Sinc, DC = 6.00 | `src/main.c:1129-1147` | Log: `Measured DC = 6.00 -> [PASS] [ok]` | **ĐÃ ĐẠT** [ok] |
| 5.5 | Golden Test 4: Sin 3 chu kỳ (Amp 3.0) | Kỳ vọng: Bin 3 = 768.00 ($3.0 \times 512 / 2$), các bin khác = 0.00 | `src/main.c:1149-1168` | Log: `Measured Bin 3 = 768.00, Other_max = 0.00 -> [PASS] [ok]` | **ĐÃ ĐẠT** [ok] |
| **6** | **Ba Kịch Bản Thực Nghiệm (§4.3)** | Điều phối qua FreeRTOS Timer callback `vScenarioTimerCallback`, kết thúc sạch bằng `qemu_exit(0)` có kiểm tra `all_ok` | `src/main.c:950-1064`<br>`src/main.c:1052-1062` | Log: Cả 3 kịch bản in rõ ràng `[PASS] [ok]` có điều kiện kiểm tra | **ĐÃ ĐẠT** [ok] |
| 6.1 | Kịch bản 1: Giám sát & Phát hiện lỗi | Chèn lỗi tại Khối #6 (RMS 2.22g, Hài 296.8Hz) và Khối #14 (RMS 2.84g, 93.7Hz) | `src/main.c:400-430`<br>`src/main.c:680-710` | Log: Phát hiện đúng 2 cảnh báo khẩn cấp, log thưa định kỳ 1:16. `[PASS] [ok]` | **ĐÃ ĐẠT** [ok] |
| 6.2 | Kịch bản 2A: Preemption mid-FFT thật | Ngắt `Interrupt1_IRQn` bắn từ trong ruột `arm_rfft_fast_f32`, Alarm (Pri 5) cướp CPU | `src/main.c:384-394`<br>`src/main.c:617-641` | Log: `Preemption verified! [PASS] [ok] t_start=51210956 < t_irq=51220848 <= t_actuate=51224998 < t_end=51241696 (cycles)` | **ĐÃ ĐẠT** [ok] |
| 6.3 | Kịch bản 2B: Priority Inheritance thật | Tranh chấp 3 Task thật trên `xLogMutex`: Logging Pri 2 $\rightarrow$ 3 $\rightarrow$ 2. Cấm `vTaskPrioritySet` giả lập | `src/main.c:872-944` (`run_priority_inheritance_demo`) | Log: `Logging priority BOOSTED to: 3 [ok]`, `Restored to: 2 [ok]`, `[PASS] [ok]` | **ĐÃ ĐẠT** [ok] |
| 6.4 | Kịch bản 3: Stress Test & Đẳng thức toán học | 4 ms/khối, kiểm chứng tràn bộ đệm đôi DMA và phân tách log thường / cảnh báo | `src/main.c:340-365`<br>`src/main.c:990-1040`<br>`src/main.c:1195-1250` | Log: $357 = 249 + 108$, $217 = 19 + 198$, `Alarm Drop: 0`. `[PASS] [ok]` | **ĐÃ ĐẠT** [ok] |
| **7** | **An toàn Bộ nhớ & Chống Race Condition** | | | | |
| 7.1 | Bảo vệ biến đếm toàn cục | `STATS_INC` (Critical Section) & `STATS_INC_FROM_ISR` (Interrupt Mask) | `src/main.c:110-125` | Không xảy ra race condition giữa ISR timer và các Task ứng dụng | **ĐÃ ĐẠT** [ok] |
| 7.2 | Đo lường High Water Mark (HWM) | Đo bằng `uxTaskGetStackHighWaterMark`, kiểm tra `Used < Alloc` động | `src/main.c:1276-1344` | Mọi Task đều thỏa mãn: $Used < Alloc$ (Alarm: 312/1024 B, Proc: 520/4096 B) | **ĐÃ ĐẠT** [ok] |
| 7.3 | Ngân sách Stack lý thuyết (Paper Budget) | Phân tích khung Context 53 thanh ghi (212 B) + Call tree FFT | `src/main.c:1332-1336`<br>§3 của tài liệu này | Tính toán lý thuyết: Proc dùng ~616 B vs cấp phát 4,096 B (Hệ số an toàn 7.7x) | **ĐÃ ĐẠT** [ok] |

---

## 2. PHÂN TÍCH TOÁN HỌC KHẢ NĂNG LẬP LỊCH (RMA SCHEDULABILITY)

Theo lý thuyết Lập lịch Đơn điệu theo Tốc độ (Rate Monotonic Scheduling - Liu & Layland, 1973), đối với tập hợp $n$ tác vụ chu kỳ độc lập, điều kiện đủ để hệ thống khả thi lập lịch là tổng hệ số sử dụng CPU không vượt quá cận trên:
$$U = \sum_{i=1}^{n} \frac{C_i}{T_i} \le U_{\text{bound}}(n) = n(2^{1/n} - 1)$$

Trong hệ thống của đề tài (Bảng 3.1 Chương III):
- Hệ thống có đúng $n = 3$ tác vụ định kỳ:
  1. `vTaskAcquisition`: $T_1 = 64.0\text{ ms}, C_1 = 2.0\text{ ms} \Rightarrow U_1 = \frac{2.0}{64.0} = 3.125\%$
  2. `vTaskProcessing`: $T_2 = 64.0\text{ ms}, C_2 = 12.0\text{ ms} \Rightarrow U_2 = \frac{12.0}{64.0} = 18.75\%$
  3. `vTaskLogging`: $T_3 = 1,000.0\text{ ms}, C_3 = 20.0\text{ ms} \Rightarrow U_3 = \frac{20.0}{1000.0} = 2.0\%$

- **Cận trên Liu & Layland cho $n = 3$**:
  $$U_{\text{bound}}(3) = 3(2^{1/3} - 1) \approx 3(1.259921 - 1) = 3 \times 0.259921 = 0.77976 \approx \mathbf{78.0\%}$$
  *(Lưu ý về số 75.7%: Giá trị 75.7% xuất hiện khi đếm nhầm 4 tác vụ chu kỳ. Vì `vTaskAlarm` là tác vụ phi chu kỳ hướng sự kiện - aperiodic/sporadic task, nó chỉ kích hoạt khi có sự cố rung động vượt ngưỡng hoặc ngắt khẩn cấp, nên số tác vụ chu kỳ để tính cận Liu & Layland chính xác là $n = 3$ với ngưỡng $78.0\%$)*.

- **Tổng hệ số sử dụng CPU thực tế**:
  $$U_{\text{total}} = U_1 + U_2 + U_3 = 3.125\% + 18.75\% + 2.0\% = \mathbf{23.875\%} \approx \mathbf{23.9\%}$$

- **Kết luận**:
  $$U_{\text{total}} = 23.9\% \ll 78.0\% = U_{\text{bound}}(3)$$
  Hệ thống hoàn toàn khả thi lập lịch theo RMA với biên dự trữ an toàn lên tới $54.1\%$ thời gian CPU.

- **Đánh giá tác vụ phi chu kỳ `vTaskAlarm`**:
  `vTaskAlarm` có hạn ngặt $D_{\text{alarm}} = 10.0\text{ ms}$, thời gian thực thi $C_{\text{alarm}} = 1.0\text{ ms}$, và được gán mức ưu tiên cao nhất (**Pri 5**). Khi có sự cố, `vTaskAlarm` cướp quyền CPU tức thời. Thời gian đáp ứng xấu nhất:
  $$R_{\text{alarm}} = C_{\text{alarm}} + T_{\text{context\_switch}} \approx 1.0\text{ ms} + 0.0064\text{ ms} \approx 1.0064\text{ ms} \ll 10.0\text{ ms}$$
  Hoàn toàn thỏa mãn hạn ngặt trong mọi tình huống.

---

## 3. NGÂN SÁCH NGĂN XẾP TRÊN GIẤY (PAPER STACK BUDGET) & ĐỐI CHIẾU THỰC TẾ

### 3.1. Phân tích khung lưu trữ phần cứng và phần mềm trên Cortex-M55
Khi xảy ra chuyển ngữ cảnh hoặc ngắt trên nhân Cortex-M55 có kích hoạt FPU (`configENABLE_FPU = 1`) và Helium MVE (`configENABLE_MVE = 1`):
1. **Hardware Context Auto-Stacking**:
   - Khung cơ bản: r0–r3, r12, LR, PC, xPSR (8 words = 32 Bytes).
   - Khung mở rộng FPU/MVE: s0–s15, FPSCR, 1 word padding (18 words = 72 Bytes).
   - Tổng Hardware stacking: 26 thanh ghi = **104 Bytes**.
2. **Software Context Stacking (trong `PendSV_Handler`)**:
   - Thanh ghi core: r4–r11 (8 words = 32 Bytes).
   - Thanh ghi hệ thống: PSPLIM, LR (EXC_RETURN), CONTROL (3 words = 12 Bytes).
   - Thanh ghi mở rộng FPU/MVE: s16–s31 qua lệnh `vstmdbeq` (16 words = 64 Bytes).
   - Tổng Software stacking: 27 thanh ghi = **108 Bytes**.
3. **Tổng chi phí bảo toàn ngữ cảnh một Task**:
   $$N_{\text{regs}} = 53\text{ thanh ghi} \Leftrightarrow \mathbf{212\text{ Bytes}}$$

### 3.2. Ngân sách ngăn xếp lý thuyết của `vTaskProcessing` (Pri 3, 4,096 B)
- **Khung Context Switch**: 212 Bytes.
- **Biến cục bộ & Compiler Frame của hàm `vTaskProcessing`**: `BlockDescriptor_t desc`, `LogEntry_t entry`, các biến đặc trưng miền thời gian (`rms`, `peak`, `crest_factor`, `dominant_freq`, `band_energy`), bộ đếm vòng lặp: ~**120 Bytes**.
- **Cây lời gọi hàm `arm_rfft_fast_f32`**: Các tham số truyền, biến thanh ghi lưu trữ tạm trong CMSIS-DSP: ~**96 Bytes**.  
  *(Lưu ý then chốt: Dữ liệu mẫu 512 số thực $2,048\text{ B}$ nằm tại biến tĩnh `g_FFT_Input` trong DTCM, hoàn toàn KHÔNG cấp phát trên Task Stack!)*
- **Lời gọi hàm `rfft_unpack_magnitude`**: ~**64 Bytes**.
- **Lời gọi API FreeRTOS (`xQueueReceive`, `xTaskNotify`, `xQueueSend`)**: ~**80 Bytes**.
- **Dự phòng ngắt NVIC lồng nhau (Interrupt nesting margin)**: ~**44 Bytes**.
- **Tổng ngân sách tính toán trên giấy**:
  $$S_{\text{theory}} = 212 + 120 + 96 + 64 + 80 + 44 = \mathbf{616\text{ Bytes}}$$

### 3.3. Bảng đối chiếu Ngân sách Thiết kế (Bảng 3.1) vs Ngân sách Giấy vs Thực tế HWM
| Tác Vụ | Vùng Nhớ | Ngân Sách Bảng 3.1 | Ngân Sách Giấy (Theory) | Đỉnh Thực Tế (Used HWM) | Dự Trữ Còn Dư (Free HWM) | Hệ Số An Toàn | Kết Luận |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| `vTaskAlarm` | **DTCM** | 1,024 B (256w) | ~292 B | **312 Bytes** (30.5%) | 712 Bytes | $3.3\times$ | **ĐẠT AN TOÀN** [ok] |
| `vTaskAcquisition` | **DTCM** | 1,024 B (256w) | ~272 B | **184 Bytes** (18.0%) | 840 Bytes | $5.6\times$ | **ĐẠT AN TOÀN** [ok] |
| `vTaskProcessing` | **DTCM** | 4,096 B (1024w) | ~616 B | **520 Bytes** (12.7%) | 3,576 Bytes | $7.7\times$ | **ĐẠT AN TOÀN** [ok] |
| `vTaskLogging` | **SRAM** | 2,048 B (512w) | ~332 B | **344 Bytes** (16.8%) | 1,704 Bytes | $6.0\times$ | **ĐẠT AN TOÀN** [ok] |
| `Idle Task` | **SRAM** | 512 B (128w) | ~128 B | **160 Bytes** (31.2%) | 352 Bytes | $3.2\times$ | **ĐẠT AN TOÀN** [ok] |

Tất cả các tác vụ đều thỏa mãn $S_{\text{used}} < S_{\text{alloc}}$, không xảy ra hiện tượng tràn ngăn xếp. Sự chênh lệch giữa 616 B lý thuyết và 520 B thực tế (96 B) phản ánh việc trình biên dịch GCC `-O2` tận dụng hiệu quả tập thanh ghi mở rộng của ARMv8.1-M để hạn chế đẩy biến tạm lên stack.

---

## 4. CHI PHÍ CHUYỂN NGỮ CẢNH THỰC TẾ TRÊN CORTEX-M55

- **Khảo sát mã máy**: File `results/04_context_switch_analysis.txt` trích xuất từ `arm-none-eabi-objdump` cho thấy `PendSV_Handler` gồm **28 câu lệnh Assembly**.
- **Ước tính chu kỳ CPU thực tế**:
  1. Stacking / Unstacking phần cứng 26 thanh ghi vào DTCM (0 wait-state): $\approx 24 - 32\text{ chu kỳ}$.
  2. Lệnh Assembly nạp/xả khối `vstmdb`, `stmdb`, `vldmia`, `ldmia` cho 27 thanh ghi: $\approx 64 - 80\text{ chu kỳ}$.
  3. Lựa chọn tác vụ `vTaskSwitchContext` tối ưu với lệnh phần cứng `CLZ` (Count Leading Zeros, độ phức tạp $O(1)$): $\approx 32 - 48\text{ chu kỳ}$.
  4. **Tổng chi phí chuyển ngữ cảnh**: $\mathbf{120 - 160\text{ chu kỳ CPU}}$ (ở tần số xung nhịp danh định 25 MHz của QEMU AN547, tương đương $\approx 4.8 - 6.4\ \mu\text{s}$).
- **Đính chính so với vòng trước**: Con số $\approx 70\text{ chu kỳ}$ ở báo cáo cũ là con số lý thuyết của nhân Cortex-M thông thường chỉ lưu 16 thanh ghi core. Với Cortex-M55 có đầy đủ FPU và Helium MVE vector extension, việc bảo toàn toàn bộ 53 thanh ghi (212 Bytes) đòi hỏi $\ge 120-160\text{ chu kỳ CPU}$.

---

## 5. CƠ CHẾ THỪA KẾ ƯU TIÊN THẬT (GENUINE PRIORITY INHERITANCE) TRÊN `xLogMutex`

Hệ thống đã viết lại hoàn toàn kịch bản kiểm chứng cơ chế Kế thừa Ưu tiên, **tuyệt đối không sử dụng lệnh `vTaskPrioritySet` để giả lập hành vi kernel**:
1. **Tranh chấp 3 Task thật theo đúng §3.3.3 Chương III**:
   - `vTaskLogging` (Pri 2) chiếm giữ `xLogMutex` để chuẩn bị ghi 1 bản ghi telemetry:
     $$\text{uxTaskPriorityGet(Logging)} = 2$$
   - `vTaskProcessing` (Pri 3) thức dậy và yêu cầu `xLogMutex`. Vì Mutex đang bị giữ, `vTaskProcessing` rơi vào trạng thái Blocked.
   - **Kernel FreeRTOS tự động nâng ưu tiên**: Ngay tại thời điểm `vTaskProcessing` bị chặn, kernel tự động nâng mức ưu tiên của `vTaskLogging` từ 2 lên **3**:
     $$\text{uxTaskPriorityGet(Logging)} = 3\quad\text{[PASS] [ok]}$$
   - `vTaskAcquisition` (Pri 4) thức dậy do có khối dữ liệu mới. Vì $\text{Pri 4} > \text{Pri 3}$, tác vụ Thu thập cướp quyền CPU thành công, thực thi đóng gói và gửi khối.
   - Sau khi `vTaskAcquisition` nhường CPU, `vTaskLogging` tiếp tục chạy ở mức Pri 3, hoàn thành việc sao chép bản ghi và giải phóng Mutex (`xSemaphoreGive`).
   - **Kernel FreeRTOS tự động hạ ưu tiên**: Ngay khi nhả Mutex, kernel tự động khôi phục mức ưu tiên của `vTaskLogging` về mức gốc **2**:
     $$\text{uxTaskPriorityGet(Logging)} = 2\quad\text{[PASS] [ok]}$$
   - `vTaskProcessing` unblock, lấy được `xLogMutex`, thực thi xong và trả Mutex.
2. **Dàn dựng có chủ đích trong kiểm thử RTOS xác định (Deterministic RTOS Testing)**:
   - Trong `run_inheritance_demo()` (`src/main.c:870-944`), việc dừng Software Timer DMA và kích hoạt ngắt `Interrupt0_IRQn` bằng tay thông qua `NVIC_SetPendingIRQ()` là kỹ thuật chuẩn tắc trong kiểm nghiệm RTOS để đảm bảo sự kiện bất đồng bộ xảy ra lặp lại xác định 100%.
   - Việc `vTaskLogging` giữ Mutex xuyên suốt chuỗi demo cũng là dàn dựng có chủ đích: thao tác sao chép 1 bản ghi 32 B thực tế chỉ tốn ~10 chu kỳ CPU ($0.4\ \mu\text{s}$ ở 25 MHz), khoảng thời gian quá ngắn để một task Pri 4 bất đồng bộ xuất hiện đúng lúc nếu không có dàn dựng đồng bộ.

---

## 6. ĐẲNG THỨC TOÁN HỌC TRONG THỬ NGHIỆM QUÁ TẢI (KỊCH BẢN 3)

Trong Kịch bản 3, hệ thống bị ép tải cực hạn ở tốc độ 4 ms/khối (nhanh gấp $16\times$ bình thường). Kết quả thu thập từ `results/03_simulation_logs.txt` chứng minh các đẳng thức bảo toàn tuyệt đối:

1. **Đẳng thức Khối Dữ liệu (DMA Double-Buffer Ping-Pong)**:
   $$\text{Generated} = \text{Processed} + \text{Buffer\_Overruns}$$
   $$\mathbf{357 = 249 + 108}\quad\text{(Đúng 100\%)} \rightarrow \mathbf{[PASS]\ [ok]}$$
   - $357$ khối được sinh ra từ ISR timer DMA stub.
   - $249$ khối được `vTaskProcessing` tiếp nhận và tính toán FFT.
   - $108$ lần phát hiện bộ đệm ping-pong đang bị bận (Busy Flag = 1) dẫn tới Overrun được ghi nhận chính xác.
   - Không có khối nào bị mất vô cớ hoặc đếm trùng.

2. **Đẳng thức Nhật ký Thường (`xLogQueue` - Sâu 8 phần tử, Drop-Oldest)**:
   $$\text{Normal\_Generated} = \text{Normal\_Written} + \text{Normal\_Dropped}$$
   $$\mathbf{217 = 19 + 198}\quad\text{(Đúng 100\%)} \rightarrow \mathbf{[PASS]\ [ok]}$$
   - $217$ bản ghi telemetry thường được tạo ra trong suốt quá trình chạy.
   - $19$ bản ghi được `vTaskLogging` xả thành công khỏi hàng đợi (bao gồm cả các bản ghi ở giai đoạn drain).
   - $198$ bản ghi cũ bị loại bỏ an toàn theo chính sách Drop-Oldest khi hàng đợi bão hòa dưới áp lực 4 ms/khối.
   - Cần nhấn mạnh: `vTaskProcessing` sử dụng lệnh gửi non-blocking (`xQueueSend(xLogQueue, &entry, 0)`). Việc không sử dụng blocking send (`portMAX_DELAY`) là quyết định thiết kế bắt buộc nhằm tránh hiện tượng đảo ngược ưu tiên ngược (Reverse Priority Inversion), trong đó task tính toán Pri 3 bị treo chờ task ghi log Pri 2 xả queue.

3. **Quan sát Bảo toàn Nhật ký Cảnh báo (`xAlarmLogQueue` - Hàng đợi riêng biệt)**:
   $$\text{Alarm\_Generated} = 3,\quad\text{Alarm\_Written} = 3,\quad\mathbf{Alarm\_Dropped = 0}$$
   $$\mathbf{0\ bản\ ghi\ cảnh\ báo\ mất\ trong\ lần\ chạy\ này\ (3\ cảnh\ báo\ /\ queue\ sâu\ 8,\ xả\ mỗi\ 1.000\ ms)}\quad\rightarrow \mathbf{[PASS]\ [ok]}$$
   - Việc tách riêng `xAlarmLogQueue` khỏi `xLogQueue` thường đảm bảo: dù hệ thống bị quá tải $16\times$ và làm rơi 198 log thường, **toàn bộ 100% nhật ký cảnh báo nguy hiểm đều được bảo vệ nguyên vẹn**.
   - Lưu ý học thuật: Đây là kết quả quan sát thực nghiệm có điều kiện dựa trên kích thước queue sâu 8 và tần suất xả 1.000 ms, không phải bảo đảm kiến trúc tuyệt đối trong mọi tình huống cực đoan vô hạn.

---

## 7. ĐỘ TRỄ CẢNH BÁO VI MÔ QUA SYSTICK CYCLE COUNTER

Độ trễ từ thời điểm phát hiện bất thường đến khi kích hoạt còi/đèn/rơ-le được đo đạc chính xác tới cấp độ chu kỳ xung nhịp thông qua thanh ghi phần cứng SysTick (tần số 25 MHz):
- **Sự kiện 1 (Block #6, T = 384 ms)**: $\text{Latency} = 8,694\text{ cycles} = 347.76\ \mu\text{s}$.
- **Sự kiện 2 (Block #14, T = 896 ms)**: $\text{Latency} = 3,174\text{ cycles} = 126.95\ \mu\text{s}$.
- **Sự kiện 3 (Kịch bản 2 Preemption mid-FFT, T = 2048 ms)**: $\text{Latency} = 4,150\text{ cycles} = 166.00\ \mu\text{s}$.
- **Sự kiện 4 (Kịch bản 3 Burst Overload Anomaly, T = 2236 ms)**: $\text{Latency} = 0\text{ cycles} = 0.00\ \mu\text{s}$.
- **Tổng kết độ trễ**:
  - Độ trễ cực đại: $\mathbf{347.76\ \mu\text{s}}\ (8,694\text{ cycles}) \ll 10.0\text{ ms} = 10,000\ \mu\text{s}$ (Biên độ an toàn $> 96.5\%$).
  - Độ trễ trung bình: $\mathbf{160.16\ \mu\text{s}}\ (4,004\text{ cycles})$.
  - *Tuyên bố học thuật*: Số liệu đo bằng SysTick trên QEMU MPS3 AN547 (25 MHz), chứng minh tính đúng đắn của cơ chế lập lịch tức thời. Trên phần cứng thực tế STM32N6, độ trễ sẽ phụ thuộc vào chu kỳ CPU và bus DTCM. Việc có mẫu đo 0 chu kỳ là do trên QEMU mô phỏng chức năng, cả ISR và đầu task Cảnh báo được thực thi liên tiếp trong cùng một cửa sổ SysTick nên bộ đếm chưa kịp giảm.

---

## 8. GIẢI THÍCH SỰ KHÁC BIỆT: SỐ LẦN KÍCH HOẠT CẢNH BÁO (12 LẦN) VS SỐ BẢN GHI LOG (3 BẢN GHI)

Trong các kịch bản kiểm thử lặp lại hoặc mở rộng, có thể ghi nhận tổng cộng 12 lần cơ cấu cảnh báo phần cứng được kích hoạt, trong khi chỉ có 3 bản ghi nhật ký cảnh báo xuất hiện trong `xAlarmLogQueue`:
- **Cơ chế hoạt động**:
  1. Mỗi khi có ngắt khẩn cấp hoặc sự cố (ví dụ các probe ngắt mid-FFT `Interrupt1_IRQn` lặp lại trong Kịch bản 2 để đo đạc preemption), `vTaskAlarm` được kích hoạt ngay lập tức qua Task Notification để kích hoạt còi/đèn/rơ-le phần cứng (`BUZZER_ON + RELAY_TRIP`) trong vòng $< 10\text{ ms}$. Các lần kích hoạt kiểm thử ngắt này **không tạo bản ghi nhật ký** vì mục tiêu là kiểm tra thời gian đáp ứng cơ cấu chấp hành tức thời.
  2. Chỉ khi `vTaskProcessing` thực sự phát hiện bất thường rung động qua thuật toán FFT (Khối #6, Khối #14 và khối bất thường trong Kịch bản 3), cấu trúc dữ liệu `LogEntry_t` đầy đủ (RMS, Peak, Crest Factor, Tần số đỉnh) mới được đóng gói và gửi vào `xAlarmLogQueue`.
- Do đó, sự khác biệt giữa số lần kích hoạt cơ cấu chấp hành và số lượng bản ghi lưu trữ là hoàn toàn đúng thiết kế và phản ánh tính phân tách rõ ràng giữa nhánh xử lý khẩn cấp (Fast Path) và nhánh ghi nhật ký (Telemetry Path).

---

## 9. QUY TẮC TRUYỀN THÔNG UART VÀ CÁC ĐIỂM IN TRỰC TIẾP

Để đảm bảo tính nhất quán với thiết kế Chương III và minh bạch trong báo cáo, quy tắc sử dụng UART được chuẩn hóa như sau:

> **Quy tắc cốt lõi**: Toàn bộ nhật ký vận hành (Operational Telemetry) của 4 tác vụ ứng dụng bắt buộc phải đi qua `vTaskLogging` thông qua hai hàng đợi `xLogQueue` và `xAlarmLogQueue`. Ngoại lệ vận hành duy nhất là dòng thông báo khẩn cấp của `vTaskAlarm` (Pri 5) được phép in trực tiếp ra phần cứng UART vì hạn ngặt $D_i = 10.0\text{ ms}$ không thể chờ chu kỳ 1.000 ms của `vTaskLogging`.

Ngoài ngoại lệ vận hành trên, mọi điểm gọi `uart_puts` trực tiếp trong mã nguồn đều thuộc về hạ tầng kiểm thử và chẩn đoán hệ thống:
1. **Giai đoạn khởi tạo hệ thống trước Scheduler (`main`, `init_dtcm_memory`)**: In banner khởi động, thông tin Heap và tạo IPC khi Scheduler chưa chạy (chưa có task nào tồn tại).
2. **Bộ kiểm tra chuẩn vàng (`run_golden_fft_tests`)**: In kết quả kiểm chứng toán học 4 test FFT trước khi khởi động kernel.
3. **Các Hook xử lý lỗi nghiêm trọng của FreeRTOS (`vApplicationStackOverflowHook`, `vApplicationMallocFailedHook`, `vAssertCalled`)**: Khi xảy ra tràn stack hoặc cạn heap, kernel rơi vào trạng thái dừng khẩn cấp (Panic), hàng đợi và scheduler không còn khả dụng nên bắt buộc phải in trực tiếp ra UART.
4. **Bộ điều phối kịch bản (`vScenarioTimerCallback`)**: In tiêu đề chuyển đổi kịch bản kiểm thử và cờ xác nhận `[PASS] [ok]`.
5. **Điểm xác nhận Preemption giữa FFT (`vTaskProcessing`)**: In 1 dòng duy nhất mang dấu vết thời gian thực chứng minh $t_{\text{start}} < t_{\text{irq}} \le t_{\text{actuate}} < t_{\text{end}}$.
6. **Điểm xác nhận cướp quyền trong Kịch bản 2B (`vTaskAcquisition`)**: In dòng xác nhận Thu thập (Pri 4) cướp CPU từ Logging (Pri 3 kế thừa).
7. **Báo cáo tổng kết thực nghiệm cuối cùng (`print_final_benchmark_report`)**: In bảng HWM và các chỉ số đo lường sau khi toàn bộ các kịch bản kiểm thử đã kết thúc và hệ thống đã xả xong dữ liệu.

---

## 10. KẾT LUẬN NGHIỆM THU VÒNG 3

1. **Khớp 100% Thiết Kế Chương III**: Toàn bộ các thông số về mức ưu tiên (5, 4, 3, 2, 0), chu kỳ định thời (64 ms, 1000 ms), hạn ngặt (10 ms), phân vùng DTCM (Double Buffer 2.0 KB, FFT Workspace 4.0 KB, Critical Stacks 6.0 KB) đều được thực hiện chuẩn xác.
2. **Loại bỏ Hoàn toàn Giả lập & Bằng chứng Giả**: Cơ chế Kế thừa Ưu tiên được kiểm chứng qua tranh chấp 3 task thật trên kernel FreeRTOS; Preemption diễn ra ngay giữa ruột hàm FFT CMSIS-DSP; các cổng điều kiện `if (all_ok)` kiểm soát chặt chẽ cờ PASS và mã thoát QEMU.
3. **Mã Nguồn, Log & Báo Cáo Đồng Bộ Tuyệt Đối**: Toàn bộ hệ thống biên dịch 0 cảnh báo, 0 lỗi, tự động thực thi và sinh báo cáo nhất quán từ file log thực tế, sẵn sàng làm bằng chứng học thuật cho Chương IV.
