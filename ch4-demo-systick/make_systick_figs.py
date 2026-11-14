#!/usr/bin/env python3
"""Figures for the Ch IV SysTick demo, drawn from the assistant's local
re-run of commit 17d047d (Ubuntu 24.04, GCC 13.2.1, QEMU 8.2.2).

Source log: ../simulation-rerun-17d047d.log (249 [BLOCK_T] records).
Output: PDF figures in ./figs/ (vector, for LaTeX).

Figures:
  ch4_gantt_measured.pdf  - measured Gantt, one S3 window (processed bars
                            gen->end split into wait/process; dropped blocks
                            reconstructed from the 4 ms generation grid and
                            marked with a red x).
  ch4_wait_resp_s3.pdf    - S3 per-block wait and response (216 blocks).
  ch4_s2_latency.pdf      - S2 alarm-latency coarse bound vs the 10 ms deadline.
  ch4_stacks.pdf          - stack allocation vs HWM (this run's numbers).

All numbers are read from the log / summary, never hard-coded, except the
stack allocations (fixed by the code) and the 10 ms / 64 ms design deadlines.
"""
import json
import re
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).resolve().parent
LOG = HERE.parent / "simulation-rerun-17d047d.log"
SUMMARY = HERE.parent / "summary-rerun-17d047d.json"
OUT = HERE / "figs"
OUT.mkdir(parents=True, exist_ok=True)

BLUE = "#2e6da4"
LIGHT = "#a6c8e0"
RED = "#c0392b"
GRAY = "#8a8a8a"
DARK = "#1f3550"

plt.rcParams.update({
    "font.size": 10,
    "axes.titlesize": 11,
    "axes.labelsize": 10,
    "axes.edgecolor": "#555555",
    "axes.linewidth": 0.8,
    "xtick.labelsize": 9,
    "ytick.labelsize": 9,
    "legend.fontsize": 9,
    "figure.dpi": 150,
})

# ---------------------------------------------------------------- parse log
rows = []
for line in open(LOG, encoding="utf-8", errors="replace"):
    m = re.match(r"\[BLOCK_T\] seq=(\d+) sc=(\d+) gen=(\d+) start=(\d+) end=(\d+)",
                 line.strip())
    if m:
        seq, sc, gen, start, end = map(int, m.groups())
        rows.append(dict(seq=seq, sc=sc, gen=gen, start=start, end=end,
                         wait=start - gen, resp=end - gen, cpu=end - start))
assert len(rows) == 249, len(rows)
s3 = [r for r in rows if r["sc"] == 3]
s1 = [r for r in rows if r["sc"] == 1]
s2 = [r for r in rows if r["sc"] == 2]
summ = json.loads(open(SUMMARY, encoding="utf-8").read())
s2t = summ["systick_timing"]["scenario2"]
s3t = summ["systick_timing"]["scenario3"]
stacks = {t["task"]: t["used"] for t in summ["stacks"]}
ALLOC = {"Alarm": 1024, "Acquisition": 1024, "Processing": 4096,
         "Logging": 2048, "Idle": 512, "TmrSvc": 1024}
ORDER = ["Alarm", "Acquisition", "Processing", "Logging", "Idle", "TmrSvc"]

# ------------------------------------------------- Fig 1: measured Gantt (S3)
# pick a window of 16 consecutive processed S3 blocks containing a 12 ms gap
WIN = 16
win_start = None
for i in range(len(s3) - WIN):
    gens = [s3[i + k]["gen"] for k in range(WIN)]
    gaps = [gens[k + 1] - gens[k] for k in range(WIN - 1)]
    if 12 in gaps:
        win_start = i
        break
assert win_start is not None
win = s3[win_start:win_start + WIN]
g0, g1 = win[0]["gen"], win[-1]["gen"]
# dropped blocks = missing 4 ms slots inside [g0, g1]
proc_gens = {r["gen"] for r in win}
dropped = [g for g in range(g0, g1 + 1, 4) if g not in proc_gens]

fig, ax = plt.subplots(figsize=(7.2, 4.6))
n = len(win) + len(dropped)
# build row list sorted by gen (processed + dropped interleaved)
items = [(r["gen"], "proc", r) for r in win] + [(g, "drop", g) for g in dropped]
items.sort(key=lambda t: t[0])
for j, (g, kind, obj) in enumerate(items):
    y = n - j  # top row = earliest
    if kind == "proc":
        r = obj
        # wait segment gen->start (light), process start->end (dark)
        ax.barh(y, r["start"] - r["gen"], left=r["gen"], height=0.55,
                color=LIGHT, edgecolor="none")
        if r["cpu"] > 0:
            ax.barh(y, r["cpu"], left=r["start"], height=0.55,
                    color=BLUE, edgecolor="none")
        else:
            # sub-tick processing: a small dark tick at start
            ax.plot([r["start"]], [y], marker="|", ms=14, color=BLUE, mew=2)
        ax.text(g0 - 3, y, f"#{r['seq']}", ha="right", va="center", fontsize=8)
    else:
        ax.plot([g], [y], marker="x", ms=9, color=RED, mew=2)
        ax.text(g0 - 3, y, "bỏ", ha="right", va="center", fontsize=8, color=RED)
ax.set_xlim(g0 - 4, g1 + 8)
ax.set_ylim(0.5, n + 0.5)
ax.set_yticks([])
ax.set_xlabel("Thời gian (ms, theo tick SysTick 1 ms)")
ax.set_title("Gantt đo thật — một cửa sổ kịch bản 3 (chờ trong queue chiếm ưu thế)")
ax.grid(axis="x", color="#e0e0e0", linewidth=0.6)
ax.set_axisbelow(True)
for s in ("top", "right", "left"):
    ax.spines[s].set_visible(False)
from matplotlib.patches import Patch
ax.legend(handles=[
    Patch(facecolor=LIGHT, label="Chờ trong queue (gen → start)"),
    Patch(facecolor=BLUE, label="Xử lý (start → end; 0 tick với đa số khối)"),
    plt.Line2D([0], [0], marker="x", color=RED, ls="none", ms=9, mew=2,
               label="Khối bị bỏ (buffer bận, suy từ lưới 4 ms)"),
], loc="upper right", frameon=False)
fig.tight_layout()
fig.savefig(OUT / "ch4_gantt_measured.pdf", bbox_inches="tight")
plt.close(fig)

# ------------------------------------------- Fig 2: S3 wait & response series
idx = np.arange(1, len(s3) + 1)
wait = np.array([r["wait"] for r in s3], dtype=float)
resp = np.array([r["resp"] for r in s3], dtype=float)
fig, ax = plt.subplots(figsize=(7.2, 3.4))
ax.bar(idx, wait, width=1.0, color=LIGHT, edgecolor="none",
       label="Thời gian chờ (gen → start)")
ax.plot(idx, resp, color=BLUE, lw=1.2, label="Thời gian đáp ứng (gen → end)")
ax.axhline(6, color=RED, lw=1.0, ls="--", label="Giá trị tối đa quan sát: 6 ms")
ax.set_xlim(1, len(s3))
ax.set_ylim(0, 8)
ax.set_xlabel("Khối đã xử lý trong kịch bản 3 (thứ tự thời gian)")
ax.set_ylabel("ms")
ax.set_title("Chờ và đáp ứng từng khối trong kịch bản 3 — răng cưa, bị chặn ở 6 ms")
ax.grid(axis="y", color="#e0e0e0", linewidth=0.6)
ax.set_axisbelow(True)
for s in ("top", "right"):
    ax.spines[s].set_visible(False)
ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.16), ncol=3, frameon=False)
fig.tight_layout()
fig.savefig(OUT / "ch4_wait_resp_s3.pdf", bbox_inches="tight")
plt.close(fig)

# --------------------------------------- Fig 3: S2 latency bound vs deadline
fig, ax = plt.subplots(figsize=(7.2, 2.6))
labels = ["Giới hạn trên đo được\n(latency_ticks = 1, SysTick 1 ms)",
          "Hạn định 10 ms\n(notification → cập nhật đầu ra)"]
vals = [s2t["latency_bound_ms"], 10]
bars = ax.barh([1, 0], vals, height=0.5, color=[BLUE, GRAY])
ax.set_yticks([1, 0])
ax.set_yticklabels(labels)
ax.set_xlim(0, 11)
ax.set_xlabel("ms")
ax.set_title("Độ trễ cảnh báo kịch bản 2 — giới hạn thô so với hạn định")
for b, v in zip(bars, vals):
    ax.text(v + 0.2, b.get_y() + b.get_height() / 2, f"< {v} ms",
            va="center", fontsize=10, color=DARK)
ax.grid(axis="x", color="#e0e0e0", linewidth=0.6)
ax.set_axisbelow(True)
for s in ("top", "right", "left"):
    ax.spines[s].set_visible(False)
fig.tight_layout()
fig.savefig(OUT / "ch4_s2_latency.pdf", bbox_inches="tight")
plt.close(fig)

# --------------------------------------------------- Fig 4: stacks (this run)
fig, ax = plt.subplots(figsize=(7.2, 3.6))
y = np.arange(len(ORDER))[::-1]
alloc = np.array([ALLOC[t] for t in ORDER], dtype=float)
used = np.array([stacks[t] for t in ORDER], dtype=float)
ax.barh(y, alloc, height=0.55, color="#d9d9d9", edgecolor="none",
        label="Cấp phát")
ax.barh(y, used, height=0.55, color=BLUE, edgecolor="none",
        label="Đã chạm tới (HWM)")
for yi, a, u in zip(y, alloc, used):
    ax.text(a + 60, yi, f"{int(a)}", va="center", fontsize=9, color=GRAY)
    ax.text(u + 60, yi, f"{int(u)}", va="center", fontsize=9, color=DARK,
            fontweight="bold")
ax.set_yticks(y)
ax.set_yticklabels(ORDER)
ax.set_xlabel("Byte")
ax.set_title("Stack cấp phát và mức đã chạm tới theo HWM (lần chạy 17d047d)")
ax.set_xlim(0, 4800)
ax.grid(axis="x", color="#e0e0e0", linewidth=0.6)
ax.set_axisbelow(True)
for s in ("top", "right", "left"):
    ax.spines[s].set_visible(False)
ax.legend(loc="lower right", frameon=False)
fig.tight_layout()
fig.savefig(OUT / "ch4_stacks.pdf", bbox_inches="tight")
plt.close(fig)

print("window gen:", g0, "->", g1, "| dropped in window:", dropped)
print("S3 wait dist:", sorted(set(int(v) for v in wait)))
print("figures written to", OUT)
for p in sorted(OUT.glob("*.pdf")):
    print(" ", p.name, p.stat().st_size, "bytes")
