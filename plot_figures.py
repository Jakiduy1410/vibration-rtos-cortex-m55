import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as patches
import numpy as np
import os

os.makedirs('results/figures', exist_ok=True)

# Set publication style
plt.rcParams['font.sans-serif'] = 'DejaVu Sans'
plt.rcParams['axes.edgecolor'] = '#333333'
plt.rcParams['axes.linewidth'] = 0.8

# ==============================================================================
# FIGURE 1: RTOS TASK TIMING & PREEMPTION DIAGRAM (GIẢN ĐỒ XUNG / ĐỊNH THỜI TASK)
# ==============================================================================
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 9), gridspec_kw={'height_ratios': [2.2, 1.3]})

# 1.1 Full Timeline (0 to 2000 ms)
tasks = ['Task_Alarm (P4)', 'Task_SensorSim (P3)', 'Task_Process (P2)', 'Task_Logger (P1)']
colors = {'Task_Alarm (P4)': '#e74c3c', 'Task_SensorSim (P3)': '#3498db',
          'Task_Process (P2)': '#2ecc71', 'Task_Logger (P1)': '#f39c12'}

ax1.set_ylim(-0.5, 3.8)
ax1.set_yticks([0, 1, 2, 3])
ax1.set_yticklabels(['Task_Logger\n(Prio 1)', 'Task_Process\n(Prio 2)', 'Task_SensorSim\n(Prio 3)', 'Task_Alarm\n(Prio 4)'], fontweight='bold', fontsize=10)
ax1.set_xlim(-20, 2050)
ax1.set_xlabel('Mô phỏng Thời gian / FreeRTOS Ticks (ms)', fontsize=11, fontweight='bold')
ax1.set_title('(A) Giản đồ xung định thời thực thi tổng thể (Task Execution & Preemption Timeline Diagram)', fontsize=13, fontweight='bold', pad=12)
ax1.grid(True, linestyle='--', alpha=0.5, axis='x')

# Scenario backgrounds
ax1.axvspan(0, 1100, color='#e8f4f8', alpha=0.5, label='Kịch bản 1: Hoạt động bình thường & Phát hiện vượt ngưỡng')
ax1.axvspan(1100, 1450, color='#fef9e7', alpha=0.5, label='Kịch bản 2: Kiểm chứng Preemption & Mutex')
ax1.axvspan(1450, 2020, color='#fdedec', alpha=0.5, label='Kịch bản 3: Stress Test (Queue Saturation & Drop Packets)')

# Plot Task Executions
# Scenario 1 (0-1000ms): Sensor runs every 50ms (takes ~1ms), Process runs after Sensor (~3ms)
for t in range(0, 1000, 50):
    # SensorSim (P3)
    ax1.broken_barh([(t, 1.5)], (1.6, 0.8), facecolors=colors['Task_SensorSim (P3)'])
    # Process (P2)
    ax1.broken_barh([(t + 1.5, 3)], (0.6, 0.8), facecolors=colors['Task_Process (P2)'])

# Alarms at sample 10 (t=450ms) and sample 16 (t=750ms)
ax1.broken_barh([(453, 3)], (2.6, 0.8), facecolors=colors['Task_Alarm (P4)'])
ax1.annotate('Emergency Alarm #1\n(Preempt CPU)', xy=(454, 3.4), xytext=(454, 3.65),
             arrowprops=dict(facecolor='red', arrowstyle='->', lw=1.2), fontsize=8, ha='center', color='red', fontweight='bold')

ax1.broken_barh([(753, 3)], (2.6, 0.8), facecolors=colors['Task_Alarm (P4)'])
ax1.annotate('Emergency Alarm #2\n(Preempt CPU)', xy=(754, 3.4), xytext=(754, 3.65),
             arrowprops=dict(facecolor='red', arrowstyle='->', lw=1.2), fontsize=8, ha='center', color='red', fontweight='bold')

# Logger (P1) every 200ms
for t in [202, 402, 602, 802, 1002, 1202, 1402, 1602, 1802, 2002]:
    ax1.broken_barh([(t, 2.5)], (-0.4, 0.8), facecolors=colors['Task_Logger (P1)'])

# Scenario 2: at t=1200
ax1.broken_barh([(1200, 1.5)], (1.6, 0.8), facecolors=colors['Task_SensorSim (P3)'])
ax1.broken_barh([(1201.5, 5)], (0.6, 0.8), facecolors=colors['Task_Process (P2)'])
ax1.broken_barh([(1203, 3)], (2.6, 0.8), facecolors=colors['Task_Alarm (P4)'])
ax1.annotate('Kịch bản 2:\nPreemption tại T0=1200', xy=(1203, 3.4), xytext=(1203, 3.65),
             arrowprops=dict(facecolor='purple', arrowstyle='->', lw=1.2), fontsize=8, ha='center', color='purple', fontweight='bold')

# Scenario 3: Sensor 2ms, Process 8ms
for t in range(1480, 1640, 2):
    ax1.broken_barh([(t, 0.8)], (1.6, 0.8), facecolors=colors['Task_SensorSim (P3)'])
for t in range(1482, 1880, 8):
    ax1.broken_barh([(t, 7)], (0.6, 0.8), facecolors=colors['Task_Process (P2)'])

ax1.broken_barh([(1540, 3)], (2.6, 0.8), facecolors=colors['Task_Alarm (P4)'])
ax1.annotate('Emergency Alarm #4\n(Queue Full: 50 Dropped)', xy=(1540, 3.4), xytext=(1540, 3.65),
             arrowprops=dict(facecolor='red', arrowstyle='->', lw=1.2), fontsize=8, ha='center', color='red', fontweight='bold')

ax1.legend(loc='lower left', bbox_to_anchor=(0.0, 1.05), ncol=3, frameon=True, fontsize=9)

# 1.2 Zoom-in Preemption Mechanism (Tick 1195 to 1215 ms)
ax2.set_xlim(1198, 1210)
ax2.set_ylim(-0.5, 3.8)
ax2.set_yticks([0, 1, 2, 3])
ax2.set_yticklabels(['Task_Logger\n(P1)', 'Task_Process\n(P2)', 'Task_SensorSim\n(P3)', 'Task_Alarm\n(P4)'], fontweight='bold', fontsize=9)
ax2.set_xlabel('Thời gian chi tiết (Tick / ms) - Zoom-in Kịch bản 2', fontsize=11, fontweight='bold')
ax2.set_title('(B) Vi mô cơ chế cướp quyền (Preemption & Context Switch Detail tại Tick T0 = 1200)', fontsize=12, fontweight='bold', pad=8)
ax2.grid(True, linestyle=':', alpha=0.7)

# Detail blocks
# SensorSim sends sample at 1200.0 - 1200.4
ax2.broken_barh([(1199.5, 0.8)], (1.6, 0.8), facecolors='#3498db', edgecolor='black')
ax2.text(1199.9, 2.0, 'QueueSend', ha='center', va='center', fontsize=7, color='white', fontweight='bold')

# Process starts at 1200.3, calculates until 1201.2, then gives Semaphore
ax2.broken_barh([(1200.3, 1.2)], (0.6, 0.8), facecolors='#2ecc71', edgecolor='black')
ax2.text(1200.9, 1.0, 'RMS > 2.5\nGive(Alarm)', ha='center', va='center', fontsize=7, color='black', fontweight='bold')

# Context Switch (PendSV) arrow
ax2.annotate('PendSV Context Switch\n(P2 -> P4 in 2.8us)', xy=(1201.5, 2.6), xytext=(1200.5, 3.2),
             arrowprops=dict(facecolor='black', arrowstyle='->', lw=1.5), fontsize=8, fontweight='bold')

# Task Alarm PREEMPTS immediately at 1201.5 to 1204.0
ax2.broken_barh([(1201.5, 2.5)], (2.6, 0.8), facecolors='#e74c3c', edgecolor='black')
ax2.text(1202.75, 3.0, 'Alarm PREEMPTED\nEmergency Log', ha='center', va='center', fontsize=8, color='white', fontweight='bold')

# Alarm blocks on semaphore -> Return to Process at 1204.0 to 1206.5
ax2.annotate('Alarm Blocked -> PendSV\nReturn to P2', xy=(1204.0, 1.4), xytext=(1204.5, 2.2),
             arrowprops=dict(facecolor='black', arrowstyle='->', lw=1.5), fontsize=8, fontweight='bold')
ax2.broken_barh([(1204.0, 2.5)], (0.6, 0.8), facecolors='#2ecc71', edgecolor='black')
ax2.text(1205.25, 1.0, 'Process Resumes\nLog finished', ha='center', va='center', fontsize=8, color='black', fontweight='bold')

plt.tight_layout()
fig.savefig('results/figures/01_task_execution_timing_diagram.png', dpi=300)
plt.close(fig)
print("Saved 01_task_execution_timing_diagram.png")

# ==============================================================================
# FIGURE 2: VIBRATION SIGNALS & ANOMALY DETECTION (DẠNG SÓNG TÍN HIỆU RUNG ĐỘNG)
# ==============================================================================
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8), sharex=True)

# Generate synthetic vibration signal based on experimental log
time_s1 = np.linspace(0, 1000, 20)
ax_s1 = np.array([0.9, 1.05, 1.2, 0.9, 1.05, 1.2, 0.9, 1.05, 1.2, 4.1, 0.9, 1.05, 1.2, 0.9, 1.05, 4.1, 0.9, 1.05, 1.2, 0.9])
ay_s1 = np.array([1.0, 1.1, 1.0, 1.1, 1.0, 1.1, 1.0, 1.1, 1.0, 4.2, 1.0, 1.1, 1.0, 1.1, 1.0, 4.2, 1.0, 1.1, 1.0, 1.1])
az_s1 = np.array([1.1, 1.05, 1.0, 0.95, 1.1, 1.05, 1.0, 0.95, 1.1, 4.25, 1.1, 1.05, 1.0, 0.95, 1.1, 4.25, 1.1, 1.05, 1.0, 0.95])
rms_s1 = np.sqrt((ax_s1**2 + ay_s1**2 + az_s1**2) / 3.0)

# Scenario 2 (1200ms)
time_s2 = np.array([1200])
ax_s2, ay_s2, az_s2 = np.array([5.0]), np.array([5.0]), np.array([5.0])
rms_s2 = np.sqrt((ax_s2**2 + ay_s2**2 + az_s2**2) / 3.0)

# Scenario 3 (1480-1640ms)
time_s3 = np.linspace(1480, 1640, 80)
ax_s3 = np.ones(80) * 1.0; ax_s3[4] = 4.8
ay_s3 = np.ones(80) * 1.1; ay_s3[4] = 4.8
az_s3 = np.ones(80) * 0.9; az_s3[4] = 4.8
rms_s3 = np.sqrt((ax_s3**2 + ay_s3**2 + az_s3**2) / 3.0)

# Subplot 1: 3-Axis Accelerations
ax1.plot(time_s1, ax_s1, 'o-', color='#3498db', label='Gia tốc trục X (ax)', linewidth=1.5, markersize=4)
ax1.plot(time_s1, ay_s1, 's-', color='#2ecc71', label='Gia tốc trục Y (ay)', linewidth=1.5, markersize=4)
ax1.plot(time_s1, az_s1, '^-', color='#9b59b6', label='Gia tốc trục Z (az)', linewidth=1.5, markersize=4)

ax1.plot(time_s2, ax_s2, 'ro', markersize=8)
ax1.plot(time_s3, ax_s3, color='#3498db', alpha=0.6, linewidth=1)
ax1.plot(time_s3, ay_s3, color='#2ecc71', alpha=0.6, linewidth=1)
ax1.plot(time_s3, az_s3, color='#9b59b6', alpha=0.6, linewidth=1)

ax1.set_ylabel('Biên độ Gia tốc (g)', fontsize=11, fontweight='bold')
ax1.set_title('(A) Dạng sóng gia tốc 3 trục giả lập thời gian thực (Raw 3-Axis Accelerations)', fontsize=12, fontweight='bold')
ax1.grid(True, linestyle='--', alpha=0.5)
ax1.legend(loc='upper right', frameon=True)

# Subplot 2: RMS Value vs Threshold
ax2.plot(time_s1, rms_s1, 'o-', color='#e67e22', label='Giá trị hiệu dụng RMS = sqrt((ax^2+ay^2+az^2)/3)', linewidth=2.0, markersize=5)
ax2.plot(time_s2, rms_s2, 'r*', markersize=12)
ax2.plot(time_s3, rms_s3, '-', color='#e67e22', linewidth=1.5)
ax2.axhline(y=2.5, color='red', linestyle='--', linewidth=2.0, label='Ngưỡng cảnh báo nguy hiểm (Threshold = 2.50g)')

# Mark anomaly events
ax2.scatter([450, 750, 1200, 1488], [4.18, 4.18, 5.0, 4.8], color='red', s=120, zorder=5)
ax2.annotate('Bất thường #1 (RMS=4.18g)\n-> Alarm #1 @ 450ms', xy=(450, 4.18), xytext=(450, 4.6),
             arrowprops=dict(facecolor='red', arrowstyle='->', lw=1.2), fontsize=8, ha='center', color='red', fontweight='bold')
ax2.annotate('Bất thường #2 (RMS=4.18g)\n-> Alarm #2 @ 750ms', xy=(750, 4.18), xytext=(750, 4.6),
             arrowprops=dict(facecolor='red', arrowstyle='->', lw=1.2), fontsize=8, ha='center', color='red', fontweight='bold')
ax2.annotate('Preemption Event\n(RMS=5.00g) @ 1200ms', xy=(1200, 5.0), xytext=(1200, 5.3),
             arrowprops=dict(facecolor='purple', arrowstyle='->', lw=1.2), fontsize=8, ha='center', color='purple', fontweight='bold')
ax2.annotate('Burst Anomaly #4\n(RMS=4.80g) @ 1488ms', xy=(1488, 4.8), xytext=(1488, 5.2),
             arrowprops=dict(facecolor='red', arrowstyle='->', lw=1.2), fontsize=8, ha='center', color='red', fontweight='bold')

ax2.set_xlabel('Thời gian mô phỏng (FreeRTOS Ticks / ms)', fontsize=11, fontweight='bold')
ax2.set_ylabel('Giá trị RMS (g)', fontsize=11, fontweight='bold')
ax2.set_title('(B) Đường biểu diễn giá trị hiệu dụng RMS và các điểm phát hiện bất thường (Anomaly Triggers)', fontsize=12, fontweight='bold')
ax2.grid(True, linestyle='--', alpha=0.5)
ax2.legend(loc='upper right', frameon=True)

plt.tight_layout()
fig.savefig('results/figures/02_vibration_signal_and_rms.png', dpi=300)
plt.close(fig)
print("Saved 02_vibration_signal_and_rms.png")

# ==============================================================================
# FIGURE 3: MEMORY & STACK HIGH WATER MARK (BIỂU ĐỒ BỘ NHỚ & TÀI NGUYÊN)
# ==============================================================================
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

# 3.1 Stack High Water Mark Breakdown
task_names = ['Task_Alarm\n(P4)', 'Task_SensorSim\n(P3)', 'Task_Process\n(P2)', 'Task_Logger\n(P1)']
alloc_stack = np.array([2048, 2048, 4096, 4096])
used_stack = np.array([172, 344, 336, 128])
free_hwm = alloc_stack - used_stack

bars_used = ax1.bar(task_names, used_stack, label='Đỉnh Stack đã sử dụng (Used Stack)', color='#e74c3c', width=0.5)
bars_free = ax1.bar(task_names, free_hwm, bottom=used_stack, label='Dự trữ an toàn (Free High Water Mark)', color='#2ecc71', width=0.5, alpha=0.8)

for bar_u, bar_f, total in zip(bars_used, bars_free, alloc_stack):
    u_h = bar_u.get_height()
    f_h = bar_f.get_height()
    ax1.text(bar_u.get_x() + bar_u.get_width()/2, u_h/2, f'{int(u_h)} B\n({u_h/total*100:.1f}%)',
             ha='center', va='center', color='white', fontweight='bold', fontsize=8)
    ax1.text(bar_f.get_x() + bar_f.get_width()/2, u_h + f_h/2, f'{int(f_h)} B\n({f_h/total*100:.1f}%)',
             ha='center', va='center', color='black', fontweight='bold', fontsize=8)
    ax1.text(bar_f.get_x() + bar_f.get_width()/2, total + 80, f'Tổng: {total} B',
             ha='center', va='bottom', fontweight='bold', fontsize=9)

ax1.set_ylim(0, 4600)
ax1.set_ylabel('Dung lượng Stack (Bytes)', fontsize=11, fontweight='bold')
ax1.set_title('(A) Phân bổ và Giới hạn an toàn Stack (Stack High Water Mark)', fontsize=12, fontweight='bold')
ax1.legend(loc='upper left', frameon=True)
ax1.grid(True, linestyle='--', alpha=0.4, axis='y')

# 3.2 FreeRTOS Heap Allocation (heap_4.c)
labels = ['Heap Đang Sử Dụng\n(16,984 B / 25.9%)', 'Heap Còn Trống\n(48,552 B / 74.1%)']
sizes = [16984, 48552]
colors_heap = ['#e67e22', '#3498db']
explode = (0.05, 0)

wedges, texts, autotexts = ax2.pie(sizes, explode=explode, labels=labels, colors=colors_heap,
                                  autopct='%1.1f%%', startangle=140, textprops=dict(fontweight='bold', fontsize=10),
                                  wedgeprops=dict(edgecolor='white', linewidth=2))
for at in autotexts:
    at.set_color('white')
ax2.set_title('(B) Tỉ lệ Sử dụng FreeRTOS Heap (Tổng cấu hình 64 KB = 65,536 Bytes)', fontsize=12, fontweight='bold')

plt.tight_layout()
fig.savefig('results/figures/03_stack_and_heap_footprint.png', dpi=300)
plt.close(fig)
print("Saved 03_stack_and_heap_footprint.png")

# ==============================================================================
# FIGURE 4: CORTEX-M55 CONTEXT SWITCH STACK FRAME ARCHITECTURE (SƠ ĐỒ STACK FRAME)
# ==============================================================================
fig, ax = plt.subplots(figsize=(10, 8))
ax.set_xlim(-1, 11)
ax.set_ylim(-0.5, 12.5)
ax.axis('off')

ax.text(5, 12.0, 'SƠ ĐỒ CẤU TRÚC STACK FRAME CORTEX-M55 KHI CONTEXT SWITCH (ARMv8.1-M + FPU/MVE)',
        fontsize=12, fontweight='bold', ha='center')

# Hardware Frame Box
rect_hw = patches.FancyBboxPatch((1, 6.2), 8, 4.8, boxstyle="round,pad=0.2", ec="#c0392b", fc="#fadbd8", lw=1.5)
ax.add_patch(rect_hw)
ax.text(1.3, 10.6, '[1] KHUNG PHẦN CỨNG TỰ ĐỘNG LƯU (HARDWARE AUTO-STACKING)', fontsize=10, fontweight='bold', color='#922b21')
ax.text(1.3, 10.1, 'Lưu tự động vào PSP khi xảy ra Exception (PendSV):', fontsize=9, style='italic')

# Core registers
rect_core = patches.Rectangle((1.4, 8.4), 7.2, 1.4, ec='#7b241c', fc='#f5b7b1', lw=1)
ax.add_patch(rect_core)
ax.text(5.0, 9.1, 'Basic Frame (8 words / 32 Bytes):\nR0, R1, R2, R3, R12, LR, Return PC, xPSR',
        ha='center', va='center', fontsize=9, fontweight='bold')

# FPU S0-S15
rect_fpu_hw = patches.Rectangle((1.4, 6.6), 7.2, 1.5, ec='#7b241c', fc='#edbb99', lw=1)
ax.add_patch(rect_fpu_hw)
ax.text(5.0, 7.35, 'Extended FPU/Helium Frame (18 words / 72 Bytes):\ns0 - s15, FPSCR, Padding (Aligner)',
        ha='center', va='center', fontsize=9, fontweight='bold')

# Software Frame Box
rect_sw = patches.FancyBboxPatch((1, 0.5), 8, 5.0, boxstyle="round,pad=0.2", ec="#2980b9", fc="#d4e6f1", lw=1.5)
ax.add_patch(rect_sw)
ax.text(1.3, 5.1, '[2] KHUNG PHẦN MỀM THỰC HIỆN TRONG PendSV_Handler (SOFTWARE STACKING)', fontsize=10, fontweight='bold', color='#1b4f72')
ax.text(1.3, 4.6, 'Thực thi bởi mã Assembly trong portasm.c của FreeRTOS:', fontsize=9, style='italic')

# FPU S16-S31
rect_fpu_sw = patches.Rectangle((1.4, 3.2), 7.2, 1.1, ec='#1f618d', fc='#aed6f1', lw=1)
ax.add_patch(rect_fpu_sw)
ax.text(5.0, 3.75, "Lệnh 'vstmdbeq r0!, {s16-s31}' (16 words / 64 Bytes):\ns16 - s31 (Thanh ghi FPU / MVE còn lại)",
        ha='center', va='center', fontsize=9, fontweight='bold')

# Core R4-R11 + System
rect_core_sw = patches.Rectangle((1.4, 0.9), 7.2, 2.0, ec='#1f618d', fc='#85c1e9', lw=1)
ax.add_patch(rect_core_sw)
ax.text(5.0, 1.9, "Lệnh 'stmdb r0!, {r2, r3, r4-r11}' (10 words / 40 Bytes):\nR4 - R11 (Thanh ghi bảo toàn)\nPSPLIM (Stack Limit) & LR (EXC_RETURN = 0xFFFFFFBC)\nLưu đỉnh PSP mới vào TCB: pxCurrentTCB->pxTopOfStack",
        ha='center', va='center', fontsize=9, fontweight='bold')

# Summary arrow
ax.annotate('', xy=(9.4, 0.5), xytext=(9.4, 11.0),
            arrowprops=dict(arrowstyle='<->', lw=2, color='#2c3e50'))
ax.text(9.6, 5.75, 'TỔNG CỘNG 53 THANH GHI\n(212 Bytes trên Task Stack)\nThời gian: 60-80 Cycles (~2.8 us)',
        va='center', fontsize=9, fontweight='bold', color='#2c3e50')

plt.tight_layout()
fig.savefig('results/figures/04_cortex_m55_context_switch_stack_frame.png', dpi=300)
plt.close(fig)
print("Saved 04_cortex_m55_context_switch_stack_frame.png")
