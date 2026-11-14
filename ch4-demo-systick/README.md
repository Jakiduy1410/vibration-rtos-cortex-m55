# Demo Chương IV — bản SysTick (nhánh xem trước, chưa merge)

Nhánh này chứa bản nháp demo Chương IV viết từ lần chạy lại commit
`17d047d` trên máy trợ lý (Ubuntu 24.04, GCC 13.2.1, QEMU 8.2.2).
Chưa merge vào `main`. Mọi số liệu đều từ `simulation.log` trong thư mục này.

## Có gì trong nhánh

- `chuong4-demo-systick.tex` — toàn bộ Chương IV dạng standalone (14 trang),
  bổ sung mục 4.4.4 đo thời gian bằng SysTick và ba hình mới.
- `make_systick_figs.py` — script vẽ 4 hình mới từ `simulation.log`.
- `simulation.log` — nhật ký QEMU của lần chạy lại (249 dòng `[BLOCK_T]`).
- `summary.json` — số đếm, stack, heap và số đo SysTick của lần chạy lại.

## Thêm được gì so với Chương IV trong báo cáo 51 trang

- Mục 4.4.4 mới: số đo thời gian SysTick (độ phân giải 1 ms).
  Độ trễ cảnh báo kịch bản 2 có giới hạn trên dưới 2 ms (nhỏ hơn hạn 10 ms);
  kịch bản 3 chờ/đáp ứng tối đa 6 ms, trung bình 2,96/2,98 ms, 0 khối trễ hạn,
  throughput 166,67 khối/giây.
- Ba hình mới vẽ từ 249 bản ghi `[BLOCK_T]`: Gantt đo thật (cửa sổ kịch bản 3),
  chờ/đáp ứng từng khối kịch bản 3 (răng cưa, bị chặn ở 6 ms),
  giới hạn độ trễ S2 so với hạn 10 ms.
- Số đếm và stack cập nhật theo lần chạy lại: 357/249/108,
  kịch bản 3 là 324/216/108, log thường 217/18/199;
  stack Processing 632 byte (GCC 13.2.1), TmrSvc 456 byte, Alarm 168 byte.
- Ảnh nạp 192216 byte (36,66% của 512 KiB).

## Cách build

Hình là file PDF nhị phân nên không đẩy lên nhánh này qua connector;
chạy script để tạo lại rồi mới biên dịch LaTeX:

```sh
python3 make_systick_figs.py        # tạo 4 hình mới vào figs/
# copy 3 hình dùng lại (spectrum, RMS, blocks) từ báo cáo 51 trang vào figs/
#   figs/ch4_spectrum.pdf, figs/ch4_rms_s1.pdf, figs/ch4_blocks.pdf
pdflatex chuong4-demo-systick.tex   # chạy 2 lần
```

Bản PDF đã build sẵn (14 trang, 0 overfull) được đính kèm trong đoạn chat
gửi nhánh này, không nằm trong repo.
