#!/usr/bin/env python3
"""
两轴云台控制界面 (Two-Axis Gimbal Control)
使用 ID=6 (Yaw) 和 ID=4 (Pitch) 舵机

使用方法:
    uv run python gimbal_control.py
"""

import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import time
from typing import Optional
from dataclasses import dataclass

# ============================================================================
# 协议常量
# ============================================================================
FRAME_HEADER = 0x55
CMD_MOVE_TIME_WRITE = 1
CMD_POS_READ = 28

# ============================================================================
# 舵机配置
# ============================================================================
@dataclass
class ServoConfig:
    id: int
    name: str
    min_pulse: int = 125
    max_pulse: int = 875
    default_pulse: int = 500
    min_angle: float = -90.0
    max_angle: float = 90.0

# 两轴配置
YAW_SERVO = ServoConfig(id=6, name="Yaw (左右)", default_pulse=500)
# Pitch: -90°~0° 对应脉冲 125~500
PITCH_SERVO = ServoConfig(id=4, name="Pitch (上下)", 
                          min_pulse=125, max_pulse=500, default_pulse=312,
                          min_angle=-90.0, max_angle=0.0)


class SerialProtocol:
    """串口通信协议"""
    
    def __init__(self):
        self.ser: Optional[serial.Serial] = None
        self.lock = threading.Lock()
    
    def open(self, port: str, baudrate: int = 115200) -> bool:
        try:
            self.ser = serial.Serial(
                port=port,
                baudrate=baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,
                write_timeout=1.0
            )
            print(f"[SERIAL] 已连接 {port}")
            time.sleep(0.2)
            return True
        except Exception as e:
            print(f"[SERIAL] 连接失败: {e}")
            return False
    
    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[SERIAL] 已断开")
    
    def is_open(self) -> bool:
        return self.ser is not None and self.ser.is_open
    
    def _calc_checksum(self, data: bytes) -> int:
        return (~sum(data)) & 0xFF
    
    def _build_packet(self, servo_id: int, cmd: int, params: bytes = b'') -> bytes:
        length = len(params) + 3
        packet = bytes([FRAME_HEADER, FRAME_HEADER, servo_id, length, cmd]) + params
        checksum = self._calc_checksum(packet[2:])
        return packet + bytes([checksum])
    
    def move_servo(self, servo_id: int, position: int, time_ms: int = 100) -> bool:
        """移动舵机到指定位置"""
        if not self.is_open():
            return False
        
        position = max(0, min(1000, position))
        time_ms = max(0, min(30000, time_ms))
        
        params = bytes([
            position & 0xFF,
            (position >> 8) & 0xFF,
            time_ms & 0xFF,
            (time_ms >> 8) & 0xFF
        ])
        
        with self.lock:
            packet = self._build_packet(servo_id, CMD_MOVE_TIME_WRITE, params)
            self.ser.reset_input_buffer()
            self.ser.write(packet)
            self.ser.flush()
            # 读取回环
            self.ser.read(len(packet))
            return True
    
    def read_position(self, servo_id: int) -> Optional[int]:
        """读取舵机当前位置"""
        if not self.is_open():
            return None
        
        with self.lock:
            packet = self._build_packet(servo_id, CMD_POS_READ)
            self.ser.reset_input_buffer()
            self.ser.write(packet)
            self.ser.flush()
            
            # 读取回环
            self.ser.read(len(packet))
            time.sleep(0.003)
            
            # 读取响应
            start = time.time()
            header_count = 0
            while time.time() - start < 0.1:
                if self.ser.in_waiting > 0:
                    b = self.ser.read(1)[0]
                    if b == FRAME_HEADER:
                        header_count += 1
                        if header_count == 2:
                            break
                    else:
                        header_count = 0
                else:
                    time.sleep(0.001)
            
            if header_count != 2:
                return None
            
            id_len = self.ser.read(2)
            if len(id_len) != 2:
                return None
            
            length = id_len[1]
            remaining = length - 1
            data = self.ser.read(remaining)
            if len(data) != remaining:
                return None
            
            # 解析位置 (有符号 16 位)
            if len(data) >= 3:
                pos = data[1] | (data[2] << 8)
                if pos > 32767:
                    pos -= 65536
                return pos
            return None


class GimbalControlGUI:
    """两轴云台控制界面"""
    
    def __init__(self):
        self.protocol = SerialProtocol()
        self.root = tk.Tk()
        self.root.title("两轴云台控制")
        self.root.geometry("500x600")
        self.root.resizable(False, False)
        
        # 当前位置
        self.yaw_pulse = YAW_SERVO.default_pulse
        self.pitch_pulse = PITCH_SERVO.default_pulse
        
        # 移动时间
        self.move_time = 100  # ms
        
        # 实时更新标志
        self.updating = False
        
        self._create_ui()
    
    def _create_ui(self):
        # ===== 串口设置 =====
        port_frame = ttk.LabelFrame(self.root, text="串口设置")
        port_frame.pack(padx=10, pady=5, fill="x")
        
        ttk.Label(port_frame, text="端口:").grid(row=0, column=0, padx=5, pady=5)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(port_frame, textvariable=self.port_var, width=15)
        self.port_combo.grid(row=0, column=1, padx=5, pady=5)
        
        ttk.Button(port_frame, text="刷新", command=self._refresh_ports).grid(row=0, column=2, padx=5)
        
        self.connect_btn = ttk.Button(port_frame, text="连接", command=self._toggle_connect)
        self.connect_btn.grid(row=0, column=3, padx=5)
        
        self._refresh_ports()
        
        # ===== Yaw 控制 =====
        yaw_frame = ttk.LabelFrame(self.root, text=f"Yaw 控制 (ID={YAW_SERVO.id}) - 左右旋转")
        yaw_frame.pack(padx=10, pady=10, fill="x")
        
        # 角度显示
        self.yaw_angle_var = tk.StringVar(value="0.0°")
        ttk.Label(yaw_frame, textvariable=self.yaw_angle_var, font=("Arial", 24, "bold")).pack(pady=5)
        
        # 脉冲显示 (先创建变量，再创建滑块)
        self.yaw_pulse_var = tk.StringVar(value=f"脉冲: {YAW_SERVO.default_pulse}")
        
        # 滑块
        self.yaw_scale = ttk.Scale(
            yaw_frame, from_=YAW_SERVO.min_pulse, to=YAW_SERVO.max_pulse,
            orient="horizontal", length=400, command=self._on_yaw_change
        )
        self.yaw_scale.set(YAW_SERVO.default_pulse)
        self.yaw_scale.pack(pady=5)
        
        ttk.Label(yaw_frame, textvariable=self.yaw_pulse_var).pack()
        
        # 快捷按钮
        yaw_btn_frame = ttk.Frame(yaw_frame)
        yaw_btn_frame.pack(pady=5)
        ttk.Button(yaw_btn_frame, text="◀ 左极限", command=lambda: self._set_yaw(YAW_SERVO.min_pulse)).pack(side=tk.LEFT, padx=5)
        ttk.Button(yaw_btn_frame, text="⬤ 中位", command=lambda: self._set_yaw(YAW_SERVO.default_pulse)).pack(side=tk.LEFT, padx=5)
        ttk.Button(yaw_btn_frame, text="右极限 ▶", command=lambda: self._set_yaw(YAW_SERVO.max_pulse)).pack(side=tk.LEFT, padx=5)
        
        # ===== Pitch 控制 =====
        pitch_frame = ttk.LabelFrame(self.root, text=f"Pitch 控制 (ID={PITCH_SERVO.id}) - 上下俯仰")
        pitch_frame.pack(padx=10, pady=10, fill="x")
        
        # 角度显示
        self.pitch_angle_var = tk.StringVar(value="0.0°")
        ttk.Label(pitch_frame, textvariable=self.pitch_angle_var, font=("Arial", 24, "bold")).pack(pady=5)
        
        # 脉冲显示 (先创建变量，再创建滑块)
        self.pitch_pulse_var = tk.StringVar(value=f"脉冲: {PITCH_SERVO.default_pulse}")
        
        # 滑块
        self.pitch_scale = ttk.Scale(
            pitch_frame, from_=PITCH_SERVO.min_pulse, to=PITCH_SERVO.max_pulse,
            orient="horizontal", length=400, command=self._on_pitch_change
        )
        self.pitch_scale.set(PITCH_SERVO.default_pulse)
        self.pitch_scale.pack(pady=5)
        
        ttk.Label(pitch_frame, textvariable=self.pitch_pulse_var).pack()
        
        # 快捷按钮
        pitch_btn_frame = ttk.Frame(pitch_frame)
        pitch_btn_frame.pack(pady=5)
        ttk.Button(pitch_btn_frame, text="▼ 下极限", command=lambda: self._set_pitch(PITCH_SERVO.min_pulse)).pack(side=tk.LEFT, padx=5)
        ttk.Button(pitch_btn_frame, text="⬤ 中位", command=lambda: self._set_pitch(PITCH_SERVO.default_pulse)).pack(side=tk.LEFT, padx=5)
        ttk.Button(pitch_btn_frame, text="上极限 ▲", command=lambda: self._set_pitch(PITCH_SERVO.max_pulse)).pack(side=tk.LEFT, padx=5)
        
        # ===== 全局控制 =====
        control_frame = ttk.LabelFrame(self.root, text="全局控制")
        control_frame.pack(padx=10, pady=10, fill="x")
        
        # 移动速度
        speed_frame = ttk.Frame(control_frame)
        speed_frame.pack(pady=5)
        ttk.Label(speed_frame, text="移动时间 (ms):").pack(side=tk.LEFT, padx=5)
        self.time_var = tk.StringVar(value="100")
        time_entry = ttk.Entry(speed_frame, textvariable=self.time_var, width=8)
        time_entry.pack(side=tk.LEFT)
        
        # 预设位置
        preset_frame = ttk.Frame(control_frame)
        preset_frame.pack(pady=5)
        ttk.Button(preset_frame, text="🏠 归中", command=self._go_center).pack(side=tk.LEFT, padx=5)
        ttk.Button(preset_frame, text="📖 读取位置", command=self._read_positions).pack(side=tk.LEFT, padx=5)
        
        # ===== 状态栏 =====
        self.status_var = tk.StringVar(value="未连接")
        status_bar = ttk.Label(self.root, textvariable=self.status_var, relief="sunken", anchor="w")
        status_bar.pack(side=tk.BOTTOM, fill="x")
    
    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports:
            self.port_combo.set(ports[0])
    
    def _toggle_connect(self):
        if self.protocol.is_open():
            self.protocol.close()
            self.connect_btn.config(text="连接")
            self.status_var.set("已断开")
        else:
            port = self.port_var.get()
            if port and self.protocol.open(port):
                self.connect_btn.config(text="断开")
                self.status_var.set(f"已连接: {port}")
                # 读取当前位置
                self._read_positions()
            else:
                messagebox.showerror("错误", f"无法连接 {port}")
    
    def _pulse_to_angle(self, pulse: int, config: ServoConfig) -> float:
        """脉冲转角度 - 线性映射"""
        # 线性插值: angle = min_angle + (pulse - min_pulse) * (max_angle - min_angle) / (max_pulse - min_pulse)
        pulse_range = config.max_pulse - config.min_pulse
        angle_range = config.max_angle - config.min_angle
        return config.min_angle + (pulse - config.min_pulse) * angle_range / pulse_range
    
    def _on_yaw_change(self, value):
        pulse = int(float(value))
        self.yaw_pulse = pulse
        angle = self._pulse_to_angle(pulse, YAW_SERVO)
        self.yaw_angle_var.set(f"{angle:.1f}°")
        self.yaw_pulse_var.set(f"脉冲: {pulse}")
        self._send_yaw()
    
    def _on_pitch_change(self, value):
        pulse = int(float(value))
        self.pitch_pulse = pulse
        angle = self._pulse_to_angle(pulse, PITCH_SERVO)
        self.pitch_angle_var.set(f"{angle:.1f}°")
        self.pitch_pulse_var.set(f"脉冲: {pulse}")
        self._send_pitch()
    
    def _get_move_time(self) -> int:
        try:
            return int(self.time_var.get())
        except ValueError:
            return 100
    
    def _send_yaw(self):
        if self.protocol.is_open():
            self.protocol.move_servo(YAW_SERVO.id, self.yaw_pulse, self._get_move_time())
    
    def _send_pitch(self):
        if self.protocol.is_open():
            self.protocol.move_servo(PITCH_SERVO.id, self.pitch_pulse, self._get_move_time())
    
    def _set_yaw(self, pulse: int):
        self.yaw_scale.set(pulse)
    
    def _set_pitch(self, pulse: int):
        self.pitch_scale.set(pulse)
    
    def _go_center(self):
        self._set_yaw(YAW_SERVO.default_pulse)
        self._set_pitch(PITCH_SERVO.default_pulse)
        self.status_var.set("已归中")
    
    def _read_positions(self):
        if not self.protocol.is_open():
            messagebox.showwarning("警告", "请先连接串口")
            return
        
        def read_thread():
            yaw_pos = self.protocol.read_position(YAW_SERVO.id)
            pitch_pos = self.protocol.read_position(PITCH_SERVO.id)
            
            def update_ui():
                if yaw_pos is not None:
                    self.yaw_scale.set(yaw_pos)
                    self.status_var.set(f"Yaw: {yaw_pos}, Pitch: {pitch_pos or '读取失败'}")
                if pitch_pos is not None:
                    self.pitch_scale.set(pitch_pos)
            
            self.root.after(0, update_ui)
        
        threading.Thread(target=read_thread, daemon=True).start()
    
    def run(self):
        self.root.mainloop()
        self.protocol.close()


def main():
    print("=" * 50)
    print("两轴云台控制工具")
    print(f"  Yaw  (ID={YAW_SERVO.id}): 左右旋转, 脉冲 {YAW_SERVO.min_pulse}~{YAW_SERVO.max_pulse}")
    print(f"  Pitch(ID={PITCH_SERVO.id}): 上下俯仰, 脉冲 {PITCH_SERVO.min_pulse}~{PITCH_SERVO.max_pulse}")
    print("=" * 50)
    
    app = GimbalControlGUI()
    app.run()
    return 0


if __name__ == '__main__':
    exit(main())
