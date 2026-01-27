#!/usr/bin/env python3
"""
总线舵机图形化测试工具 (Hiwonder Bus Servo GUI Tester)
支持多舵机管理、实时状态监控、批量控制

使用方法:
    uv run python bus_servo_gui.py
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import serial
import serial.tools.list_ports
import struct
import threading
import time
import json
from typing import Optional, Dict, List
from dataclasses import dataclass
from pathlib import Path

# ============================================================================
# 协议常量
# ============================================================================
FRAME_HEADER = 0x55
BROADCAST_ID = 0xFE

# 命令定义
CMD_MOVE_TIME_WRITE = 1
CMD_MOVE_TIME_READ = 2
CMD_MOVE_TIME_WAIT_WRITE = 7
CMD_MOVE_TIME_WAIT_READ = 8
CMD_MOVE_START = 11
CMD_MOVE_STOP = 12
CMD_ID_WRITE = 13
CMD_ID_READ = 14
CMD_ANGLE_OFFSET_ADJUST = 17
CMD_ANGLE_OFFSET_WRITE = 18
CMD_ANGLE_OFFSET_READ = 19
CMD_ANGLE_LIMIT_WRITE = 20
CMD_ANGLE_LIMIT_READ = 21
CMD_VIN_LIMIT_WRITE = 22
CMD_VIN_LIMIT_READ = 23
CMD_TEMP_MAX_LIMIT_WRITE = 24
CMD_TEMP_MAX_LIMIT_READ = 25
CMD_TEMP_READ = 26
CMD_VIN_READ = 27
CMD_POS_READ = 28
CMD_MODE_WRITE = 29
CMD_MODE_READ = 30
CMD_LOAD_WRITE = 31
CMD_LOAD_READ = 32
CMD_LED_WRITE = 33
CMD_LED_READ = 34
CMD_LED_ERROR_WRITE = 35
CMD_LED_ERROR_READ = 36


@dataclass
class ServoStatus:
    """舵机状态"""
    id: int = 0
    position: int = 0
    angle: float = 0.0
    voltage: int = 0
    temperature: int = 0
    loaded: bool = False
    led_on: bool = True
    online: bool = False
    last_update: float = 0.0


class BusServoProtocol:
    """总线舵机协议层"""
    
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
                write_timeout=1.0  # 防止写入阻塞
            )
            print(f"[SERIAL] 已打开 {port} @ {baudrate}bps")
            time.sleep(0.2)  # 等待串口稳定
            return True
        except Exception as e:
            print(f"[SERIAL] 打开串口失败: {e}")
            return False
    
    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
    
    def is_open(self) -> bool:
        return self.ser is not None and self.ser.is_open
    
    def _calc_checksum(self, data: bytes) -> int:
        return (~sum(data)) & 0xFF
    
    def _build_packet(self, servo_id: int, cmd: int, params: bytes = b'') -> bytes:
        length = len(params) + 3
        packet = bytes([FRAME_HEADER, FRAME_HEADER, servo_id, length, cmd]) + params
        checksum = self._calc_checksum(packet[2:])
        return packet + bytes([checksum])
    
    def _send_recv(self, servo_id: int, cmd: int, params: bytes = b'', 
                   timeout: float = 0.1) -> Optional[bytes]:
        if not self.is_open():
            return None
        
        with self.lock:
            packet = self._build_packet(servo_id, cmd, params)
            
            # 打印发送数据
            print(f"[TX] ID={servo_id:3d} CMD={cmd:2d} -> {packet.hex(' ').upper()}")
            
            self.ser.reset_input_buffer()
            self.ser.write(packet)
            self.ser.flush()
            
            # 半双工通信：先读取并丢弃自己发送的回环数据
            echo = self.ser.read(len(packet))
            if len(echo) > 0:
                print(f"[ECHO] {echo.hex(' ').upper()}")
            
            time.sleep(0.003)
            
            # 读取舵机响应
            start = time.time()
            header_count = 0
            
            while time.time() - start < timeout:
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
                print(f"[RX] ID={servo_id:3d} 超时，未收到响应")
                return None
            
            id_len = self.ser.read(2)
            if len(id_len) != 2:
                print(f"[RX] ID={servo_id:3d} 数据不完整 (id_len)")
                return None
            
            length = id_len[1]
            remaining = length - 1
            data = self.ser.read(remaining)
            if len(data) != remaining:
                print(f"[RX] ID={servo_id:3d} 数据不完整 (data)")
                return None
            
            response = bytes([FRAME_HEADER, FRAME_HEADER]) + id_len + data
            
            # 打印接收数据
            print(f"[RX] ID={servo_id:3d} <- {response.hex(' ').upper()}")
            
            # 校验
            checksum = self._calc_checksum(response[2:-1])
            if checksum != response[-1]:
                print(f"[RX] ID={servo_id:3d} 校验失败: 计算={checksum:02X} 收到={response[-1]:02X}")
                return None
            
            return response
    
    def _send_only(self, servo_id: int, cmd: int, params: bytes = b'') -> bool:
        if not self.is_open():
            return False
        
        with self.lock:
            packet = self._build_packet(servo_id, cmd, params)
            
            # 打印发送数据
            print(f"[TX] ID={servo_id:3d} CMD={cmd:2d} -> {packet.hex(' ').upper()} (无响应)")
            
            self.ser.reset_input_buffer()
            self.ser.write(packet)
            self.ser.flush()
            return True
    
    # ========== 读取命令 ==========
    
    def read_id(self) -> Optional[int]:
        resp = self._send_recv(BROADCAST_ID, CMD_ID_READ)
        if resp and len(resp) >= 7:
            return resp[5]
        return None
    
    def read_position(self, servo_id: int) -> Optional[int]:
        resp = self._send_recv(servo_id, CMD_POS_READ)
        if resp and len(resp) >= 8:
            return struct.unpack('<h', resp[5:7])[0]
        return None
    
    def read_voltage(self, servo_id: int) -> Optional[int]:
        resp = self._send_recv(servo_id, CMD_VIN_READ)
        if resp and len(resp) >= 8:
            return struct.unpack('<H', resp[5:7])[0]
        return None
    
    def read_temperature(self, servo_id: int) -> Optional[int]:
        resp = self._send_recv(servo_id, CMD_TEMP_READ)
        if resp and len(resp) >= 7:
            return resp[5]
        return None
    
    def read_load(self, servo_id: int) -> Optional[bool]:
        resp = self._send_recv(servo_id, CMD_LOAD_READ)
        if resp and len(resp) >= 7:
            return resp[5] == 1
        return None
    
    def read_led(self, servo_id: int) -> Optional[bool]:
        resp = self._send_recv(servo_id, CMD_LED_READ)
        if resp and len(resp) >= 7:
            return resp[5] == 0  # 0=亮, 1=灭
        return None
    
    def read_angle_offset(self, servo_id: int) -> Optional[int]:
        resp = self._send_recv(servo_id, CMD_ANGLE_OFFSET_READ)
        if resp and len(resp) >= 7:
            return struct.unpack('<b', bytes([resp[5]]))[0]
        return None
    
    def read_angle_limit(self, servo_id: int) -> Optional[tuple]:
        resp = self._send_recv(servo_id, CMD_ANGLE_LIMIT_READ)
        if resp and len(resp) >= 9:
            min_pos = struct.unpack('<H', resp[5:7])[0]
            max_pos = struct.unpack('<H', resp[7:9])[0]
            return (min_pos, max_pos)
        return None
    
    def read_vin_limit(self, servo_id: int) -> Optional[tuple]:
        resp = self._send_recv(servo_id, CMD_VIN_LIMIT_READ)
        if resp and len(resp) >= 9:
            min_vin = struct.unpack('<H', resp[5:7])[0]
            max_vin = struct.unpack('<H', resp[7:9])[0]
            return (min_vin, max_vin)
        return None
    
    def read_temp_limit(self, servo_id: int) -> Optional[int]:
        resp = self._send_recv(servo_id, CMD_TEMP_MAX_LIMIT_READ)
        if resp and len(resp) >= 7:
            return resp[5]
        return None
    
    def read_mode(self, servo_id: int) -> Optional[tuple]:
        resp = self._send_recv(servo_id, CMD_MODE_READ)
        if resp and len(resp) >= 9:
            mode = resp[5]
            speed = struct.unpack('<h', resp[6:8])[0]
            return (mode, speed)
        return None
    
    # ========== 写入命令 ==========
    
    def move(self, servo_id: int, position: int, time_ms: int) -> bool:
        position = max(0, min(1000, position))
        time_ms = max(0, min(30000, time_ms))
        params = struct.pack('<HH', position, time_ms)
        return self._send_only(servo_id, CMD_MOVE_TIME_WRITE, params)
    
    def move_angle(self, servo_id: int, angle: float, time_ms: int) -> bool:
        position = int(angle * 1000 / 240)
        return self.move(servo_id, position, time_ms)
    
    def move_prepare(self, servo_id: int, position: int, time_ms: int) -> bool:
        params = struct.pack('<HH', position, time_ms)
        return self._send_only(servo_id, CMD_MOVE_TIME_WAIT_WRITE, params)
    
    def move_start(self, servo_id: int = BROADCAST_ID) -> bool:
        return self._send_only(servo_id, CMD_MOVE_START)
    
    def stop(self, servo_id: int) -> bool:
        return self._send_only(servo_id, CMD_MOVE_STOP)
    
    def set_id(self, old_id: int, new_id: int) -> bool:
        params = bytes([new_id])
        return self._send_only(old_id, CMD_ID_WRITE, params)
    
    def set_load(self, servo_id: int, enable: bool) -> bool:
        params = bytes([1 if enable else 0])
        return self._send_only(servo_id, CMD_LOAD_WRITE, params)
    
    def set_led(self, servo_id: int, on: bool) -> bool:
        params = bytes([0 if on else 1])  # 0=亮, 1=灭
        return self._send_only(servo_id, CMD_LED_WRITE, params)
    
    def set_angle_offset(self, servo_id: int, offset: int, save: bool = False) -> bool:
        offset = max(-125, min(125, offset))
        params = struct.pack('<b', offset)
        cmd = CMD_ANGLE_OFFSET_WRITE if save else CMD_ANGLE_OFFSET_ADJUST
        return self._send_only(servo_id, cmd, params)
    
    def set_angle_limit(self, servo_id: int, min_pos: int, max_pos: int) -> bool:
        params = struct.pack('<HH', min_pos, max_pos)
        return self._send_only(servo_id, CMD_ANGLE_LIMIT_WRITE, params)
    
    def set_vin_limit(self, servo_id: int, min_mv: int, max_mv: int) -> bool:
        params = struct.pack('<HH', min_mv, max_mv)
        return self._send_only(servo_id, CMD_VIN_LIMIT_WRITE, params)
    
    def set_temp_limit(self, servo_id: int, max_temp: int) -> bool:
        params = bytes([max_temp])
        return self._send_only(servo_id, CMD_TEMP_MAX_LIMIT_WRITE, params)
    
    def set_mode(self, servo_id: int, mode: int, speed: int = 0) -> bool:
        params = struct.pack('<bh', mode, speed)
        return self._send_only(servo_id, CMD_MODE_WRITE, params)


class ServoControlGUI:
    """舵机控制图形界面"""
    
    def __init__(self):
        self.protocol = BusServoProtocol()
        self.servos: Dict[int, ServoStatus] = {}
        self.monitoring = False
        self.monitor_thread: Optional[threading.Thread] = None
        
        self._setup_ui()
    
    def _setup_ui(self):
        self.root = tk.Tk()
        self.root.title("总线舵机控制器 - Hiwonder Bus Servo Controller")
        self.root.geometry("1200x800")
        self.root.minsize(1000, 600)
        
        # 主框架
        main_frame = ttk.Frame(self.root, padding="5")
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # 左侧面板 - 连接和舵机列表
        left_frame = ttk.Frame(main_frame, width=350)
        left_frame.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 5))
        left_frame.pack_propagate(False)
        
        # 右侧面板 - 控制和日志
        right_frame = ttk.Frame(main_frame)
        right_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        self._setup_connection_panel(left_frame)
        self._setup_servo_list_panel(left_frame)
        self._setup_control_panel(right_frame)
        self._setup_log_panel(right_frame)
        
        self._refresh_ports()
    
    def _setup_connection_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="连接设置", padding="5")
        frame.pack(fill=tk.X, pady=(0, 5))
        
        # 串口选择
        port_frame = ttk.Frame(frame)
        port_frame.pack(fill=tk.X, pady=2)
        ttk.Label(port_frame, text="串口:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(port_frame, textvariable=self.port_var, width=15)
        self.port_combo.pack(side=tk.LEFT, padx=5)
        ttk.Button(port_frame, text="刷新", command=self._refresh_ports, width=6).pack(side=tk.LEFT)
        
        # 波特率
        baud_frame = ttk.Frame(frame)
        baud_frame.pack(fill=tk.X, pady=2)
        ttk.Label(baud_frame, text="波特率:").pack(side=tk.LEFT)
        self.baud_var = tk.StringVar(value="115200")
        baud_combo = ttk.Combobox(baud_frame, textvariable=self.baud_var, width=15,
                                   values=["9600", "19200", "38400", "57600", "115200", "230400"])
        baud_combo.pack(side=tk.LEFT, padx=5)
        
        # 连接按钮
        btn_frame = ttk.Frame(frame)
        btn_frame.pack(fill=tk.X, pady=5)
        self.connect_btn = ttk.Button(btn_frame, text="连接", command=self._toggle_connection)
        self.connect_btn.pack(side=tk.LEFT, padx=2)
        self.status_label = ttk.Label(btn_frame, text="未连接", foreground="gray")
        self.status_label.pack(side=tk.LEFT, padx=10)
    
    def _setup_servo_list_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="舵机列表", padding="5")
        frame.pack(fill=tk.BOTH, expand=True)
        
        # 工具栏
        toolbar = ttk.Frame(frame)
        toolbar.pack(fill=tk.X, pady=(0, 5))
        ttk.Button(toolbar, text="扫描", command=self._scan_servos, width=8).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="添加", command=self._add_servo_dialog, width=8).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="删除", command=self._remove_servo, width=8).pack(side=tk.LEFT, padx=2)
        
        # 监控开关
        self.monitor_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(toolbar, text="实时监控", variable=self.monitor_var,
                        command=self._toggle_monitoring).pack(side=tk.RIGHT)
        
        # 舵机树形列表
        columns = ("ID", "角度", "电压", "温度", "状态")
        self.servo_tree = ttk.Treeview(frame, columns=columns, show="headings", height=15)
        
        self.servo_tree.heading("ID", text="ID")
        self.servo_tree.heading("角度", text="角度 (°)")
        self.servo_tree.heading("电压", text="电压 (mV)")
        self.servo_tree.heading("温度", text="温度 (°C)")
        self.servo_tree.heading("状态", text="状态")
        
        self.servo_tree.column("ID", width=40, anchor="center")
        self.servo_tree.column("角度", width=70, anchor="center")
        self.servo_tree.column("电压", width=70, anchor="center")
        self.servo_tree.column("温度", width=70, anchor="center")
        self.servo_tree.column("状态", width=60, anchor="center")
        
        scrollbar = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=self.servo_tree.yview)
        self.servo_tree.configure(yscrollcommand=scrollbar.set)
        
        self.servo_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        self.servo_tree.bind('<<TreeviewSelect>>', self._on_servo_select)
    
    def _setup_control_panel(self, parent):
        # 使用 Notebook 创建标签页
        notebook = ttk.Notebook(parent)
        notebook.pack(fill=tk.BOTH, expand=True, pady=(0, 5))
        
        # 基本控制
        basic_frame = ttk.Frame(notebook, padding="10")
        notebook.add(basic_frame, text="基本控制")
        self._setup_basic_control(basic_frame)
        
        # 高级设置
        advanced_frame = ttk.Frame(notebook, padding="10")
        notebook.add(advanced_frame, text="高级设置")
        self._setup_advanced_control(advanced_frame)
        
        # 批量控制
        batch_frame = ttk.Frame(notebook, padding="10")
        notebook.add(batch_frame, text="批量控制")
        self._setup_batch_control(batch_frame)
    
    def _setup_basic_control(self, parent):
        # 目标舵机
        target_frame = ttk.LabelFrame(parent, text="目标舵机", padding="10")
        target_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(target_frame, text="舵机 ID:").grid(row=0, column=0, sticky="w")
        self.target_id_var = tk.StringVar(value="1")
        ttk.Entry(target_frame, textvariable=self.target_id_var, width=10).grid(row=0, column=1, padx=5)
        ttk.Button(target_frame, text="读取状态", command=self._read_servo_status).grid(row=0, column=2, padx=5)
        
        # 角度控制
        angle_frame = ttk.LabelFrame(parent, text="角度控制", padding="10")
        angle_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(angle_frame, text="目标角度 (0-240°):").grid(row=0, column=0, sticky="w")
        self.angle_var = tk.DoubleVar(value=120)
        self.angle_scale = ttk.Scale(angle_frame, from_=0, to=240, variable=self.angle_var,
                                      orient=tk.HORIZONTAL, length=300)
        self.angle_scale.grid(row=0, column=1, padx=5)
        self.angle_entry = ttk.Entry(angle_frame, textvariable=self.angle_var, width=8)
        self.angle_entry.grid(row=0, column=2, padx=5)
        
        ttk.Label(angle_frame, text="运动时间 (ms):").grid(row=1, column=0, sticky="w", pady=5)
        self.time_var = tk.IntVar(value=1000)
        ttk.Entry(angle_frame, textvariable=self.time_var, width=10).grid(row=1, column=1, sticky="w", padx=5)
        
        btn_frame = ttk.Frame(angle_frame)
        btn_frame.grid(row=2, column=0, columnspan=3, pady=10)
        ttk.Button(btn_frame, text="移动", command=self._move_servo, width=10).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="停止", command=self._stop_servo, width=10).pack(side=tk.LEFT, padx=5)
        
        # 快捷按钮
        quick_frame = ttk.Frame(angle_frame)
        quick_frame.grid(row=3, column=0, columnspan=3, pady=5)
        for angle in [0, 60, 120, 180, 240]:
            ttk.Button(quick_frame, text=f"{angle}°", width=6,
                       command=lambda a=angle: self._quick_move(a)).pack(side=tk.LEFT, padx=2)
        
        # 电机控制
        motor_frame = ttk.LabelFrame(parent, text="电机控制", padding="10")
        motor_frame.pack(fill=tk.X, pady=5)
        
        self.load_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(motor_frame, text="电机使能 (装载)", variable=self.load_var,
                        command=self._toggle_load).pack(side=tk.LEFT, padx=10)
        
        self.led_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(motor_frame, text="LED 开启", variable=self.led_var,
                        command=self._toggle_led).pack(side=tk.LEFT, padx=10)
    
    def _setup_advanced_control(self, parent):
        # ID 修改
        id_frame = ttk.LabelFrame(parent, text="修改舵机 ID (危险操作!)", padding="10")
        id_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(id_frame, text="新 ID:").grid(row=0, column=0, sticky="w")
        self.new_id_var = tk.StringVar(value="1")
        ttk.Entry(id_frame, textvariable=self.new_id_var, width=10).grid(row=0, column=1, padx=5)
        ttk.Button(id_frame, text="修改 ID", command=self._change_id).grid(row=0, column=2, padx=5)
        
        # 角度偏移
        offset_frame = ttk.LabelFrame(parent, text="角度偏移 (-125 ~ 125)", padding="10")
        offset_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(offset_frame, text="偏移值:").grid(row=0, column=0, sticky="w")
        self.offset_var = tk.IntVar(value=0)
        ttk.Scale(offset_frame, from_=-125, to=125, variable=self.offset_var,
                  orient=tk.HORIZONTAL, length=200).grid(row=0, column=1, padx=5)
        ttk.Entry(offset_frame, textvariable=self.offset_var, width=8).grid(row=0, column=2, padx=5)
        ttk.Button(offset_frame, text="调整", command=self._adjust_offset).grid(row=0, column=3, padx=5)
        ttk.Button(offset_frame, text="保存", command=self._save_offset).grid(row=0, column=4, padx=5)
        
        # 角度限制
        limit_frame = ttk.LabelFrame(parent, text="角度限制 (0-1000)", padding="10")
        limit_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(limit_frame, text="最小:").grid(row=0, column=0, sticky="w")
        self.min_angle_var = tk.IntVar(value=0)
        ttk.Entry(limit_frame, textvariable=self.min_angle_var, width=10).grid(row=0, column=1, padx=5)
        
        ttk.Label(limit_frame, text="最大:").grid(row=0, column=2, sticky="w")
        self.max_angle_var = tk.IntVar(value=1000)
        ttk.Entry(limit_frame, textvariable=self.max_angle_var, width=10).grid(row=0, column=3, padx=5)
        
        ttk.Button(limit_frame, text="读取", command=self._read_angle_limit).grid(row=0, column=4, padx=5)
        ttk.Button(limit_frame, text="设置", command=self._set_angle_limit).grid(row=0, column=5, padx=5)
        
        # 电压限制
        vin_frame = ttk.LabelFrame(parent, text="电压限制 (mV)", padding="10")
        vin_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(vin_frame, text="最小:").grid(row=0, column=0, sticky="w")
        self.min_vin_var = tk.IntVar(value=4500)
        ttk.Entry(vin_frame, textvariable=self.min_vin_var, width=10).grid(row=0, column=1, padx=5)
        
        ttk.Label(vin_frame, text="最大:").grid(row=0, column=2, sticky="w")
        self.max_vin_var = tk.IntVar(value=12000)
        ttk.Entry(vin_frame, textvariable=self.max_vin_var, width=10).grid(row=0, column=3, padx=5)
        
        ttk.Button(vin_frame, text="读取", command=self._read_vin_limit).grid(row=0, column=4, padx=5)
        ttk.Button(vin_frame, text="设置", command=self._set_vin_limit).grid(row=0, column=5, padx=5)
        
        # 温度限制
        temp_frame = ttk.LabelFrame(parent, text="温度限制", padding="10")
        temp_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(temp_frame, text="最高温度 (°C):").grid(row=0, column=0, sticky="w")
        self.max_temp_var = tk.IntVar(value=85)
        ttk.Entry(temp_frame, textvariable=self.max_temp_var, width=10).grid(row=0, column=1, padx=5)
        ttk.Button(temp_frame, text="读取", command=self._read_temp_limit).grid(row=0, column=2, padx=5)
        ttk.Button(temp_frame, text="设置", command=self._set_temp_limit).grid(row=0, column=3, padx=5)
    
    def _setup_batch_control(self, parent):
        # 批量舵机选择
        select_frame = ttk.LabelFrame(parent, text="选择舵机", padding="10")
        select_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(select_frame, text="ID 列表 (逗号分隔):").pack(side=tk.LEFT)
        self.batch_ids_var = tk.StringVar(value="1,2,3,4,5,6")
        ttk.Entry(select_frame, textvariable=self.batch_ids_var, width=30).pack(side=tk.LEFT, padx=5)
        
        # 同步运动
        sync_frame = ttk.LabelFrame(parent, text="同步运动 (预设+启动)", padding="10")
        sync_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(sync_frame, text="角度列表 (逗号分隔):").grid(row=0, column=0, sticky="w")
        self.batch_angles_var = tk.StringVar(value="120,120,120,120,120,120")
        ttk.Entry(sync_frame, textvariable=self.batch_angles_var, width=40).grid(row=0, column=1, padx=5)
        
        ttk.Label(sync_frame, text="运动时间 (ms):").grid(row=1, column=0, sticky="w", pady=5)
        self.batch_time_var = tk.IntVar(value=1000)
        ttk.Entry(sync_frame, textvariable=self.batch_time_var, width=10).grid(row=1, column=1, sticky="w", padx=5)
        
        ttk.Button(sync_frame, text="执行同步运动", command=self._batch_sync_move).grid(row=2, column=0, 
                                                                                        columnspan=2, pady=10)
        
        # 批量操作
        batch_btn_frame = ttk.LabelFrame(parent, text="批量操作", padding="10")
        batch_btn_frame.pack(fill=tk.X, pady=5)
        
        ttk.Button(batch_btn_frame, text="全部使能", command=lambda: self._batch_load(True)).pack(side=tk.LEFT, padx=5)
        ttk.Button(batch_btn_frame, text="全部卸载", command=lambda: self._batch_load(False)).pack(side=tk.LEFT, padx=5)
        ttk.Button(batch_btn_frame, text="全部停止", command=self._batch_stop).pack(side=tk.LEFT, padx=5)
        ttk.Button(batch_btn_frame, text="LED 全开", command=lambda: self._batch_led(True)).pack(side=tk.LEFT, padx=5)
        ttk.Button(batch_btn_frame, text="LED 全关", command=lambda: self._batch_led(False)).pack(side=tk.LEFT, padx=5)
    
    def _setup_log_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="日志", padding="5")
        frame.pack(fill=tk.X, pady=5)
        
        self.log_text = scrolledtext.ScrolledText(frame, height=8, wrap=tk.WORD)
        self.log_text.pack(fill=tk.BOTH, expand=True)
        
        btn_frame = ttk.Frame(frame)
        btn_frame.pack(fill=tk.X, pady=2)
        ttk.Button(btn_frame, text="清除日志", command=lambda: self.log_text.delete(1.0, tk.END)).pack(side=tk.RIGHT)
    
    def _log(self, message: str):
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert(tk.END, f"[{timestamp}] {message}\n")
        self.log_text.see(tk.END)
    
    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])
    
    def _toggle_connection(self):
        if self.protocol.is_open():
            self.monitoring = False
            if self.monitor_thread:
                self.monitor_thread.join(timeout=1)
            self.protocol.close()
            self.connect_btn.config(text="连接")
            self.status_label.config(text="未连接", foreground="gray")
            self._log("断开连接")
        else:
            port = self.port_var.get()
            baud = int(self.baud_var.get())
            if self.protocol.open(port, baud):
                self.connect_btn.config(text="断开")
                self.status_label.config(text=f"已连接 {port}", foreground="green")
                self._log(f"连接到 {port} @ {baud}")
            else:
                messagebox.showerror("错误", f"无法打开串口 {port}")
    
    def _scan_servos(self):
        if not self.protocol.is_open():
            messagebox.showwarning("警告", "请先连接串口")
            return
        
        # 弹出对话框选择扫描范围
        scan_range = self._ask_scan_range()
        if scan_range is None:
            return
        
        start_id, end_id = scan_range
        
        # 在后台线程中扫描
        def scan_thread():
            try:
                self._log(f"开始扫描舵机 (ID {start_id}-{end_id})...")
                self.servos.clear()
                
                # 清空列表 (在主线程中执行)
                self.root.after(0, lambda: [self.servo_tree.delete(item) 
                                            for item in self.servo_tree.get_children()])
                
                found_count = 0
                for servo_id in range(start_id, end_id + 1):
                    try:
                        pos = self.protocol.read_position(servo_id)
                        if pos is not None:
                            status = ServoStatus(id=servo_id, position=pos, 
                                                angle=pos * 240.0 / 1000.0, online=True)
                            self.servos[servo_id] = status
                            self.root.after(0, self._update_servo_tree, servo_id)
                            self._log(f"发现舵机 ID={servo_id}, 位置={pos}")
                            found_count += 1
                    except Exception as e:
                        print(f"[SCAN] ID={servo_id} 错误: {e}")
                        break  # 串口错误时停止扫描
                    
                    # 舵机间延迟 (参考 6dof-visual-arm-control)
                    time.sleep(0.05)
                
                self._log(f"扫描完成，发现 {found_count} 个舵机")
            except Exception as e:
                import traceback
                print(f"[SCAN ERROR] {e}")
                traceback.print_exc()
                self._log(f"扫描错误: {e}")
        
        threading.Thread(target=scan_thread, daemon=True).start()
    
    def _ask_scan_range(self):
        """弹出对话框让用户选择扫描范围"""
        dialog = tk.Toplevel(self.root)
        dialog.title("扫描范围")
        dialog.geometry("250x120")
        dialog.transient(self.root)
        dialog.grab_set()
        
        result = [None]
        
        ttk.Label(dialog, text="起始 ID:").grid(row=0, column=0, padx=10, pady=5)
        start_var = tk.StringVar(value="1")
        ttk.Entry(dialog, textvariable=start_var, width=10).grid(row=0, column=1, padx=10, pady=5)
        
        ttk.Label(dialog, text="结束 ID:").grid(row=1, column=0, padx=10, pady=5)
        end_var = tk.StringVar(value="10")
        ttk.Entry(dialog, textvariable=end_var, width=10).grid(row=1, column=1, padx=10, pady=5)
        
        def on_ok():
            try:
                start = int(start_var.get())
                end = int(end_var.get())
                if 1 <= start <= 253 and 1 <= end <= 253 and start <= end:
                    result[0] = (start, end)
                    dialog.destroy()
                else:
                    messagebox.showerror("错误", "ID 范围必须在 1-253 之间")
            except ValueError:
                messagebox.showerror("错误", "请输入有效数字")
        
        def on_cancel():
            dialog.destroy()
        
        btn_frame = ttk.Frame(dialog)
        btn_frame.grid(row=2, column=0, columnspan=2, pady=10)
        ttk.Button(btn_frame, text="扫描", command=on_ok).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="取消", command=on_cancel).pack(side=tk.LEFT, padx=5)
        
        dialog.wait_window()
        return result[0]
    
    def _add_servo_dialog(self):
        dialog = tk.Toplevel(self.root)
        dialog.title("添加舵机")
        dialog.geometry("200x100")
        dialog.transient(self.root)
        
        ttk.Label(dialog, text="舵机 ID:").pack(pady=5)
        id_var = tk.StringVar(value="1")
        ttk.Entry(dialog, textvariable=id_var, width=10).pack()
        
        def add():
            try:
                servo_id = int(id_var.get())
                if servo_id not in self.servos:
                    self.servos[servo_id] = ServoStatus(id=servo_id)
                    self._update_servo_tree(servo_id)
                dialog.destroy()
            except ValueError:
                messagebox.showerror("错误", "请输入有效的 ID")
        
        ttk.Button(dialog, text="添加", command=add).pack(pady=10)
    
    def _remove_servo(self):
        selected = self.servo_tree.selection()
        if selected:
            item = selected[0]
            servo_id = int(self.servo_tree.item(item)['values'][0])
            if servo_id in self.servos:
                del self.servos[servo_id]
            self.servo_tree.delete(item)
    
    def _update_servo_tree(self, servo_id: int):
        status = self.servos.get(servo_id)
        if not status:
            return
        
        values = (
            status.id,
            f"{status.angle:.1f}",
            status.voltage,
            status.temperature,
            "在线" if status.online else "离线"
        )
        
        # 检查是否已存在
        for item in self.servo_tree.get_children():
            if int(self.servo_tree.item(item)['values'][0]) == servo_id:
                self.servo_tree.item(item, values=values)
                return
        
        self.servo_tree.insert('', tk.END, values=values)
    
    def _on_servo_select(self, event):
        selected = self.servo_tree.selection()
        if selected:
            item = selected[0]
            servo_id = int(self.servo_tree.item(item)['values'][0])
            self.target_id_var.set(str(servo_id))
    
    def _toggle_monitoring(self):
        if self.monitor_var.get():
            if not self.protocol.is_open():
                self.monitor_var.set(False)
                messagebox.showwarning("警告", "请先连接串口")
                return
            self.monitoring = True
            self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
            self.monitor_thread.start()
            self._log("开始实时监控")
        else:
            self.monitoring = False
            self._log("停止实时监控")
    
    def _monitor_loop(self):
        while self.monitoring and self.protocol.is_open():
            for servo_id in list(self.servos.keys()):
                if not self.monitoring:
                    break
                
                status = self.servos[servo_id]
                
                pos = self.protocol.read_position(servo_id)
                if pos is not None:
                    status.position = pos
                    status.angle = pos * 240.0 / 1000.0
                    status.online = True
                else:
                    status.online = False
                
                vin = self.protocol.read_voltage(servo_id)
                if vin is not None:
                    status.voltage = vin
                
                temp = self.protocol.read_temperature(servo_id)
                if temp is not None:
                    status.temperature = temp
                
                status.last_update = time.time()
                
                # 更新 UI
                self.root.after(0, self._update_servo_tree, servo_id)
                time.sleep(0.05)
            
            time.sleep(0.5)
    
    def _get_target_id(self) -> int:
        try:
            return int(self.target_id_var.get())
        except ValueError:
            return 1
    
    def _read_servo_status(self):
        if not self.protocol.is_open():
            messagebox.showwarning("警告", "请先连接串口")
            return
        
        servo_id = self._get_target_id()
        
        pos = self.protocol.read_position(servo_id)
        vin = self.protocol.read_voltage(servo_id)
        temp = self.protocol.read_temperature(servo_id)
        load = self.protocol.read_load(servo_id)
        led = self.protocol.read_led(servo_id)
        offset = self.protocol.read_angle_offset(servo_id)
        
        if pos is not None:
            angle = pos * 240.0 / 1000.0
            status = ServoStatus(
                id=servo_id, position=pos, angle=angle,
                voltage=vin or 0, temperature=temp or 0,
                loaded=load if load is not None else False,
                led_on=led if led is not None else True,
                online=True
            )
            self.servos[servo_id] = status
            self._update_servo_tree(servo_id)
            
            self._log(f"舵机 {servo_id}: 角度={angle:.1f}°, 电压={vin}mV, "
                     f"温度={temp}°C, 使能={load}, LED={led}, 偏移={offset}")
            
            self.load_var.set(load if load is not None else True)
            self.led_var.set(led if led is not None else True)
            if offset is not None:
                self.offset_var.set(offset)
        else:
            self._log(f"舵机 {servo_id}: 无响应")
    
    def _move_servo(self):
        if not self.protocol.is_open():
            messagebox.showwarning("警告", "请先连接串口")
            return
        
        servo_id = self._get_target_id()
        angle = self.angle_var.get()
        time_ms = self.time_var.get()
        
        if self.protocol.move_angle(servo_id, angle, time_ms):
            self._log(f"舵机 {servo_id}: 移动到 {angle:.1f}° ({time_ms}ms)")
        else:
            self._log(f"舵机 {servo_id}: 移动命令发送失败")
    
    def _stop_servo(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        self.protocol.stop(servo_id)
        self._log(f"舵机 {servo_id}: 停止")
    
    def _quick_move(self, angle: float):
        self.angle_var.set(angle)
        self._move_servo()
    
    def _toggle_load(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        enable = self.load_var.get()
        self.protocol.set_load(servo_id, enable)
        self._log(f"舵机 {servo_id}: {'使能' if enable else '卸载'}")
    
    def _toggle_led(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        on = self.led_var.get()
        self.protocol.set_led(servo_id, on)
        self._log(f"舵机 {servo_id}: LED {'开' if on else '关'}")
    
    def _change_id(self):
        if not self.protocol.is_open():
            messagebox.showwarning("警告", "请先连接串口")
            return
        
        old_id = self._get_target_id()
        try:
            new_id = int(self.new_id_var.get())
        except ValueError:
            messagebox.showerror("错误", "请输入有效的新 ID")
            return
        
        if not messagebox.askyesno("确认", f"确定要将舵机 {old_id} 的 ID 修改为 {new_id}？"):
            return
        
        if self.protocol.set_id(old_id, new_id):
            self._log(f"舵机 ID 已从 {old_id} 修改为 {new_id}")
            if old_id in self.servos:
                self.servos[new_id] = self.servos.pop(old_id)
                self.servos[new_id].id = new_id
        else:
            self._log(f"修改舵机 ID 失败")
    
    def _adjust_offset(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        offset = self.offset_var.get()
        self.protocol.set_angle_offset(servo_id, offset, save=False)
        self._log(f"舵机 {servo_id}: 调整偏移为 {offset}")
    
    def _save_offset(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        offset = self.offset_var.get()
        self.protocol.set_angle_offset(servo_id, offset, save=True)
        self._log(f"舵机 {servo_id}: 保存偏移 {offset}")
    
    def _read_angle_limit(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        limit = self.protocol.read_angle_limit(servo_id)
        if limit:
            self.min_angle_var.set(limit[0])
            self.max_angle_var.set(limit[1])
            self._log(f"舵机 {servo_id}: 角度限制 {limit[0]} - {limit[1]}")
    
    def _set_angle_limit(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        min_val = self.min_angle_var.get()
        max_val = self.max_angle_var.get()
        self.protocol.set_angle_limit(servo_id, min_val, max_val)
        self._log(f"舵机 {servo_id}: 设置角度限制 {min_val} - {max_val}")
    
    def _read_vin_limit(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        limit = self.protocol.read_vin_limit(servo_id)
        if limit:
            self.min_vin_var.set(limit[0])
            self.max_vin_var.set(limit[1])
            self._log(f"舵机 {servo_id}: 电压限制 {limit[0]} - {limit[1]} mV")
    
    def _set_vin_limit(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        min_val = self.min_vin_var.get()
        max_val = self.max_vin_var.get()
        self.protocol.set_vin_limit(servo_id, min_val, max_val)
        self._log(f"舵机 {servo_id}: 设置电压限制 {min_val} - {max_val} mV")
    
    def _read_temp_limit(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        limit = self.protocol.read_temp_limit(servo_id)
        if limit:
            self.max_temp_var.set(limit)
            self._log(f"舵机 {servo_id}: 温度限制 {limit}°C")
    
    def _set_temp_limit(self):
        if not self.protocol.is_open():
            return
        
        servo_id = self._get_target_id()
        max_temp = self.max_temp_var.get()
        self.protocol.set_temp_limit(servo_id, max_temp)
        self._log(f"舵机 {servo_id}: 设置温度限制 {max_temp}°C")
    
    def _get_batch_ids(self) -> List[int]:
        try:
            return [int(x.strip()) for x in self.batch_ids_var.get().split(',')]
        except ValueError:
            return []
    
    def _batch_sync_move(self):
        if not self.protocol.is_open():
            messagebox.showwarning("警告", "请先连接串口")
            return
        
        ids = self._get_batch_ids()
        try:
            angles = [float(x.strip()) for x in self.batch_angles_var.get().split(',')]
        except ValueError:
            messagebox.showerror("错误", "角度列表格式错误")
            return
        
        if len(ids) != len(angles):
            messagebox.showerror("错误", "ID 数量与角度数量不匹配")
            return
        
        time_ms = self.batch_time_var.get()
        
        # 预设所有舵机
        for servo_id, angle in zip(ids, angles):
            position = int(angle * 1000 / 240)
            self.protocol.move_prepare(servo_id, position, time_ms)
        
        time.sleep(0.05)
        
        # 同步启动
        self.protocol.move_start(BROADCAST_ID)
        self._log(f"同步运动: IDs={ids}, 角度={angles}, 时间={time_ms}ms")
    
    def _batch_load(self, enable: bool):
        if not self.protocol.is_open():
            return
        
        for servo_id in self._get_batch_ids():
            self.protocol.set_load(servo_id, enable)
            time.sleep(0.01)
        
        self._log(f"批量{'使能' if enable else '卸载'}: {self._get_batch_ids()}")
    
    def _batch_stop(self):
        if not self.protocol.is_open():
            return
        
        for servo_id in self._get_batch_ids():
            self.protocol.stop(servo_id)
            time.sleep(0.01)
        
        self._log(f"批量停止: {self._get_batch_ids()}")
    
    def _batch_led(self, on: bool):
        if not self.protocol.is_open():
            return
        
        for servo_id in self._get_batch_ids():
            self.protocol.set_led(servo_id, on)
            time.sleep(0.01)
        
        self._log(f"批量 LED {'开' if on else '关'}: {self._get_batch_ids()}")
    
    def run(self):
        self.root.mainloop()
        self.monitoring = False
        self.protocol.close()


def main():
    import traceback
    print("=" * 60)
    print("总线舵机 GUI 测试工具启动...")
    print("=" * 60)
    
    try:
        print("[DEBUG] 正在创建 GUI 窗口...")
        app = ServoControlGUI()
        print("[DEBUG] GUI 创建成功，进入主循环...")
        app.run()
        print("[DEBUG] 程序正常退出")
        return 0
    except Exception as e:
        print("\n" + "=" * 60)
        print("程序发生错误!")
        print("=" * 60)
        print(f"错误类型: {type(e).__name__}")
        print(f"错误信息: {e}")
        print("\n详细堆栈:")
        traceback.print_exc()
        print("=" * 60)
        input("按 Enter 键退出...")
        return 1


if __name__ == '__main__':
    try:
        exit(main())
    except Exception as e:
        import traceback
        print(f"启动失败: {e}")
        traceback.print_exc()
        input("按 Enter 键退出...")
        exit(1)
