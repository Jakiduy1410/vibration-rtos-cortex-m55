# Giám sát rung động C/FreeRTOS trên Cortex-M55

Bốn tác vụ: Alarm (5), Acquisition (4), Processing (3), Logging (2); kernel có Idle và TmrSvc. Nguồn mẫu tổng hợp 8 kHz logic, 512 mẫu/khối; nhịp danh định 64 ms. Hai buffer DTCM được giữ bằng cờ bận; queue truyền mô tả/con trỏ. Nguồn bỏ lượt mới khi buffer bận. Queue mô tả gửi không chặn, đếm riêng nếu đầy.

## Xây dựng và chạy

Cần GNU Arm GCC và QEMU trên PATH. Chạy `mingw32-make` (hoặc `make`), sau đó `python verify_reconciled.py --qemu <qemu-system-arm>`. Cấu hình CPU/SysTick là 32 MHz, APB của AN547 là 25 MHz. Bản đã kiểm tra dùng GCC 13.3.1 xPack và QEMU 11.1.0; chi tiết và hash ELF trong `results/reconciled/summary.json`. Không commit mới; đây là thay đổi cục bộ trên nền 90ce744.

## Ba kịch bản

1. Cửa sổ 2000 ms, hai khối bất thường #6/#14 và kiểm tra luồng dữ liệu.
2. Ngắt nhân tạo khi FFT đang chạy, kiểm tra Alarm chạy trước khi FFT trở lại, đồng thời kiểm tra RMS/bin sau tiếp tục; mutex là bài thử chuyên biệt với trình tự Logging 2→3→2.
3. Nhịp yêu cầu 64→4 ms (16 lần), Processing chờ 6 tick và log mỗi khối để tạo nghẽn có chủ đích. Đây không phải tăng tải CPU 16 lần.

## Phạm vi kết quả

Dùng **results/reconciled/** làm nguồn kết quả mới. Những báo cáo và hình ở results/ gốc là bằng chứng của bản cũ, không dùng cho bản báo cáo đã sửa. Các script chạy, gỡ lỗi và sinh báo cáo theo định dạng cũ đã được loại bỏ; dùng `verify_reconciled.py` để chạy và kiểm tra ba kịch bản hiện tại.

Bản mới kiểm tra chức năng, số đếm và HWM; không xuất CPU%, RTA, WCET hoặc độ trễ micro giây từ bộ đếm SysTick cũ. Mục tiêu 10 ms vẫn là yêu cầu chưa xác nhận định lượng. Tickless bật nhưng chưa thử điện năng. Cảnh báo cập nhật biến đầu ra mô phỏng, không điều khiển GPIO STM32 thật. Ngưỡng phổ dùng biên độ một phía chuẩn hóa; peak là max(abs(sample)).

Heap_5 hiện dùng một vùng 64 KiB, không mặc định cần nhiều vùng. Cấp phát stack/TCB tĩnh nằm ngoài heap. Không suy ra không rò rỉ hoặc an toàn tuyệt đối từ một lần chạy. Thư viện CMSIS-DSP 1.10.0 đi cùng gói CMSIS 5.9.0; nguồn có hook FFT phục vụ kiểm thử.
