#!/usr/bin/env python3
"""
S300固件下载工具 - ESP32风格的进度条实现
支持固件下载(RBL)和OTA更新(APP)的进度显示

类似于ESP32的idf.py flash，进度条在PC端实现，不在芯片内部
"""

import sys
import os
import time
import argparse
from pathlib import Path
from typing import Optional, Callable, List, Dict
import struct

# 自动串口检测
try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False
    print("Warning: pyserial not installed. Install with: pip install pyserial")

# 颜色输出支持
try:
    from colorama import init, Fore, Style
    init()
    HAS_COLOR = True
except ImportError:
    HAS_COLOR = False
    
    class _DummyColor:
        RED = GREEN = YELLOW = BLUE = MAGENTA = CYAN = WHITE = ""
    
    class _DummyStyle:
        BRIGHT = DIM = RESET_ALL = ""
        
    Fore = _DummyColor()
    Style = _DummyStyle()

# 进度条字符
PROGRESS_CHARS = {
    'filled': '█',
    'empty': '░', 
    'partial': ['▏', '▎', '▍', '▌', '▋', '▊', '▉'],
    'spinner': ['|', '/', '-', '\\']
}

# Ymodem协议常量
YMODEM_SOH = 0x01      # 128字节包
YMODEM_STX = 0x02      # 1K字节包  
YMODEM_EOT = 0x04      # 传输结束
YMODEM_ACK = 0x06      # 确认
YMODEM_NAK = 0x15      # 否认
YMODEM_CAN = 0x18      # 取消
YMODEM_C = 0x43        # 请求CRC模式

class ProgressBar:
    """ESP32风格的进度条实现"""
    
    def __init__(self, total: int, width: int = 50, show_percentage: bool = True, 
                 show_speed: bool = True, desc: str = "Progress", color: str = "GREEN"):
        self.total = total
        self.current = 0
        self.width = width
        self.show_percentage = show_percentage
        self.show_speed = show_speed
        self.desc = desc
        self.color = color
        self.start_time = time.time()
        self.last_update_time = self.start_time
        self.last_current = 0
        self.speed_history = []
        self.failed = False
        
    def update(self, current: int) -> None:
        """更新进度"""
        self.current = min(current, self.total)
        self._render()
        
    def set_failed(self, failed: bool = True) -> None:
        """设置失败状态"""
        self.failed = failed
        self._render()
        
    def _render(self) -> None:
        """渲染进度条"""
        now = time.time()
        
        # 计算百分比
        if self.total > 0:
            percentage = (self.current * 100) // self.total
            filled_width = (self.current * self.width) // self.total
        else:
            percentage = 0
            filled_width = 0
            
        # 构建进度条
        bar = PROGRESS_CHARS['filled'] * filled_width
        bar += PROGRESS_CHARS['empty'] * (self.width - filled_width)
        
        # 构建显示字符串
        parts = [f"\r{self.desc}: ["]
        
        # 添加颜色
        if HAS_COLOR:
            if self.failed:
                color = Fore.RED
            elif self.color == "GREEN":
                color = Fore.GREEN
            elif self.color == "YELLOW":
                color = Fore.YELLOW
            elif self.color == "BLUE":
                color = Fore.BLUE
            else:
                color = Fore.GREEN
                
            parts.append(f"{color}{bar[:filled_width]}{Style.RESET_ALL}")
            parts.append(f"{Fore.WHITE}{bar[filled_width:]}{Style.RESET_ALL}")
        else:
            parts.append(bar)
            
        parts.append("]")
        
        # 百分比
        if self.show_percentage:
            if self.failed:
                parts.append(f" {Fore.RED}FAILED{Style.RESET_ALL}")
            else:
                parts.append(f" {percentage:3d}%")
            
        # 数据量显示
        if self.total > 0:
            current_mb = self.current / 1024 / 1024
            total_mb = self.total / 1024 / 1024
            if total_mb >= 1:
                parts.append(f" ({current_mb:.1f}/{total_mb:.1f} MB)")
            else:
                current_kb = self.current / 1024
                total_kb = self.total / 1024
                parts.append(f" ({current_kb:.0f}/{total_kb:.0f} KB)")
                
        # 传输速度
        if self.show_speed and now > self.last_update_time + 0.5 and not self.failed:
            elapsed = now - self.last_update_time
            bytes_transferred = self.current - self.last_current
            
            if elapsed > 0:
                speed = bytes_transferred / elapsed
                self.speed_history.append(speed)
                
                # 保留最近10个速度样本
                if len(self.speed_history) > 10:
                    self.speed_history.pop(0)
                    
                avg_speed = sum(self.speed_history) / len(self.speed_history)
                
                if avg_speed > 1024 * 1024:
                    parts.append(f" | {avg_speed / 1024 / 1024:.1f} MB/s")
                elif avg_speed > 1024:
                    parts.append(f" | {avg_speed / 1024:.0f} KB/s")
                else:
                    parts.append(f" | {avg_speed:.0f} B/s")
                    
                # ETA计算
                if avg_speed > 0 and self.current < self.total:
                    remaining_bytes = self.total - self.current
                    eta_seconds = remaining_bytes / avg_speed
                    if eta_seconds < 60:
                        parts.append(f" | ETA: {eta_seconds:.0f}s")
                    else:
                        eta_minutes = eta_seconds / 60
                        parts.append(f" | ETA: {eta_minutes:.1f}m")
                        
            self.last_update_time = now
            self.last_current = self.current
            
        print(''.join(parts), end='', flush=True)
        
        # 完成时换行
        if self.current >= self.total or self.failed:
            print()

def list_serial_ports() -> List:
    """列出可用串口"""
    if not HAS_SERIAL:
        print(f"{Fore.YELLOW}Warning: pyserial not available{Style.RESET_ALL}")
        return []
    
    try:
        ports = list(serial.tools.list_ports.comports())
        # 过滤并排序串口，优先显示常见的开发板串口
        filtered_ports = []
        priority_ports = []
        
        for port in ports:
            desc = getattr(port, 'description', '').lower()
            vid = getattr(port, 'vid', 0)
            pid = getattr(port, 'pid', 0)
            
            # 检查是否是常见的开发板串口芯片
            is_dev_board = any([
                'ch34' in desc,  # CH340/CH341
                'cp210' in desc,  # CP2102/CP2104
                'ft232' in desc,  # FTDI
                'pl2303' in desc,  # PL2303
                vid in [0x1a86, 0x10c4, 0x0403, 0x067b],  # 常见VID
                'usb-serial' in desc,
                'uart' in desc
            ])
            
            if is_dev_board:
                priority_ports.append(port)
            else:
                filtered_ports.append(port)
        
        # 返回排序后的串口列表（开发板串口优先）
        return priority_ports + filtered_ports
        
    except Exception as e:
        print(f"{Fore.YELLOW}Warning: Failed to enumerate ports: {e}{Style.RESET_ALL}")
        return []

def select_serial_port() -> Optional[str]:
    """让用户选择串口"""
    ports = list_serial_ports()
    if not ports:
        print(f"{Fore.RED}✗{Style.RESET_ALL} No serial ports detected. Use -p to specify manually.")
        return None
        
    print(f"{Fore.CYAN}Available serial ports:{Style.RESET_ALL}")
    for idx, port in enumerate(ports):
        desc = getattr(port, 'description', 'Unknown device')
        vid_pid = ""
        if hasattr(port, 'vid') and hasattr(port, 'pid') and port.vid and port.pid:
            vid_pid = f" (VID:PID={port.vid:04X}:{port.pid:04X})"
        
        # 标记可能的开发板串口
        is_likely_dev = any([
            'ch34' in desc.lower(),
            'cp210' in desc.lower(), 
            'ft232' in desc.lower(),
            'usb-serial' in desc.lower()
        ])
        
        marker = f" {Fore.GREEN}[Likely Dev Board]{Style.RESET_ALL}" if is_likely_dev else ""
        print(f"  [{idx+1}] {port.device} - {desc}{vid_pid}{marker}")
        
    # 如果只有一个明显的开发板串口，提供快速选择
    dev_ports = [i for i, port in enumerate(ports) if any([
        'ch34' in getattr(port, 'description', '').lower(),
        'cp210' in getattr(port, 'description', '').lower(),
        'ft232' in getattr(port, 'description', '').lower(),
        'usb-serial' in getattr(port, 'description', '').lower()
    ])]
    
    if len(dev_ports) == 1:
        print(f"\n{Fore.GREEN}Detected likely development board port: {ports[dev_ports[0]].device}{Style.RESET_ALL}")
        auto_choice = input(f"{Fore.YELLOW}Use this port? [Y/n]: {Style.RESET_ALL}").strip().lower()
        if auto_choice in ['', 'y', 'yes']:
            return ports[dev_ports[0]].device
        
    while True:
        try:
            selection = input(f"\n{Fore.YELLOW}Select port number (1-{len(ports)}): {Style.RESET_ALL}")
            sel_idx = int(selection) - 1
            if 0 <= sel_idx < len(ports):
                return ports[sel_idx].device
            else:
                print(f"{Fore.RED}Invalid selection. Please choose 1-{len(ports)}{Style.RESET_ALL}")
        except (ValueError, KeyboardInterrupt):
            print(f"\n{Fore.RED}Operation cancelled{Style.RESET_ALL}")
            return None

class S300Downloader:
    """S300固件下载器"""
    
    def __init__(self, port: str, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.download_mode = False
        
    def connect(self) -> bool:
        """连接到S300设备，并自动DTR/RTS复位"""
        if not HAS_SERIAL:
            print(f"{Fore.RED}✗{Style.RESET_ALL} pyserial not available")
            return False
            
        try:
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=3.0,
                write_timeout=3.0
            )
            
            # DTR/RTS自动复位序列
            print(f"{Fore.YELLOW}Performing DTR/RTS reset...{Style.RESET_ALL}", end='', flush=True)
            try:
                # ESP32风格的复位序列
                self.serial_conn.dtr = False
                self.serial_conn.rts = True
                time.sleep(0.1)
                self.serial_conn.dtr = True
                self.serial_conn.rts = False
                time.sleep(0.05)
                self.serial_conn.dtr = False
                self.serial_conn.rts = False
                print(f" {Fore.GREEN}✓{Style.RESET_ALL}")
            except Exception as e:
                print(f" {Fore.YELLOW}Warning: {e}{Style.RESET_ALL}")
                
            print(f"{Fore.GREEN}✓{Style.RESET_ALL} Connected to {self.port} @ {self.baudrate} baud")
            return True
            
        except Exception as e:
            print(f"{Fore.RED}✗{Style.RESET_ALL} Failed to connect: {e}")
            return False
            
    def disconnect(self) -> None:
        """断开连接"""
        if self.serial_conn:
            self.serial_conn.close()
            self.serial_conn = None
            
    def probe_device(self) -> bool:
        """探测和验证S300设备"""
        print(f"{Fore.CYAN}Probing S300 device...{Style.RESET_ALL}")
        
        # 尝试建立基本通信
        try:
            # 发送基本握手序列 
            self.serial_conn.write(b'\x0C')  # Form Feed - 清屏信号
            time.sleep(0.1)
            self.serial_conn.write(b'?')     # 查询命令
            time.sleep(0.2)
            
            # 检查响应
            response = self.serial_conn.read_all()
            if response:
                print(f"{Fore.GREEN}✓{Style.RESET_ALL} Device responds to commands")
                return True
                
            # 尝试Ymodem启动序列
            self.serial_conn.write(b'C')
            time.sleep(0.1)
            response = self.serial_conn.read(1)
            if response == b'\x15':  # NAK
                print(f"{Fore.GREEN}✓{Style.RESET_ALL} Device ready for Ymodem transfer")
                return True
                
            print(f"{Fore.YELLOW}⚠{Style.RESET_ALL} Device detection uncertain, continuing...")
            return True  # 允许继续尝试
            
        except Exception as e:
            print(f"{Fore.YELLOW}⚠{Style.RESET_ALL} Device probe failed: {e}")
            return True  # 仍然允许继续尝试

    def get_device_info(self) -> Dict[str, str]:
        """获取设备信息"""
        info = {
            'chip': 'S300',
            'arch': 'ARM Cortex-M4',
            'bootloader': 'RBL/SBL',
            'protocol': 'Ymodem'
        }
        
        try:
            # 尝试获取更多设备信息
            self.serial_conn.write(b'i')  # Info command
            time.sleep(0.1)
            response = self.serial_conn.read_all()
            if response:
                response_str = response.decode('utf-8', errors='ignore')
                # 解析版本信息等
                if 'version' in response_str.lower():
                    info['version'] = response_str.strip()
        except:
            pass
            
        return info
            
    def enter_download_mode(self) -> bool:
        """进入下载模式"""
        if not self.serial_conn:
            return False
            
        print(f"{Fore.YELLOW}Entering download mode...{Style.RESET_ALL}")
        
        # 清空缓冲区
        self.serial_conn.reset_input_buffer()
        self.serial_conn.reset_output_buffer()
        
        try:
            # 方法1: 发送下载命令
            download_commands = [b"DOWNLOAD\r\n", b"download\r\n", b"D\r\n"]
            for cmd in download_commands:
                self.serial_conn.write(cmd)
                time.sleep(0.2)
            
            # 方法2: 等待RBL响应
            print("Waiting for bootloader response...", end='', flush=True)
            for i in range(15):  # 15秒超时
                self.serial_conn.write(b"\r\n")
                response = self.serial_conn.read(100)
                
                # 检查是否收到下载模式响应
                response_str = response.decode('ascii', errors='ignore').lower()
                if any(keyword in response_str for keyword in ['rbl', 'download', 'ymodem', 'ready']):
                    self.download_mode = True
                    print(f" {Fore.GREEN}✓{Style.RESET_ALL}")
                    print(f"{Fore.GREEN}✓{Style.RESET_ALL} Device entered download mode")
                    return True
                    
                time.sleep(0.5)
                print(".", end='', flush=True)
                
            print(f" {Fore.RED}✗{Style.RESET_ALL}")
            print(f"{Fore.YELLOW}! Please manually reset device and try again{Style.RESET_ALL}")
            print(f"{Fore.YELLOW}! Or check if device is already in download mode{Style.RESET_ALL}")
            return False
            
        except Exception as e:
            print(f"{Fore.RED}✗{Style.RESET_ALL} Failed to enter download mode: {e}")
            return False
            
    def send_ymodem_file(self, file_path: str, progress_callback: Optional[Callable] = None) -> bool:
        """发送Ymodem文件"""
        if not self.serial_conn or not self.download_mode:
            return False
            
        try:
            with open(file_path, 'rb') as f:
                file_data = f.read()
                
            file_name = os.path.basename(file_path)
            file_size = len(file_data)
            
            print(f"{Fore.CYAN}Sending file: {file_name} ({file_size} bytes){Style.RESET_ALL}")
            
            # 等待设备发送'C'请求
            print("Waiting for device ready signal...", end='', flush=True)
            for _ in range(30):  # 30秒超时
                if self.serial_conn.in_waiting > 0:
                    data = self.serial_conn.read(1)
                    if data == bytes([YMODEM_C]):
                        print(f" {Fore.GREEN}✓{Style.RESET_ALL}")
                        break
                time.sleep(1)
                print(".", end='', flush=True)
            else:
                print(f" {Fore.RED}✗ Timeout{Style.RESET_ALL}")
                return False
                
            # 发送文件信息包
            if not self._send_file_info_packet(file_name, file_size):
                return False
                
            # 创建进度条
            if progress_callback is None:
                progress_bar = ProgressBar(file_size, desc="Downloading")
                progress_callback = progress_bar.update
                
            # 发送文件数据
            success = self._send_file_data(file_data, progress_callback)
            
            if success:
                # 发送空包结束传输
                self._send_end_packet()
                
            return success
            
        except Exception as e:
            print(f"{Fore.RED}✗{Style.RESET_ALL} File transfer failed: {e}")
            return False
            
    def _send_file_info_packet(self, file_name: str, file_size: int) -> bool:
        """发送文件信息包"""
        # 构建文件信息字符串
        file_info = f"{file_name}\x00{file_size}\x00"
        file_info_bytes = file_info.encode('ascii')
        
        # 补齐到128字节
        packet_data = file_info_bytes + b'\x00' * (128 - len(file_info_bytes))
        
        # 构建数据包
        packet = bytearray()
        packet.append(YMODEM_SOH)  # 包头
        packet.append(0)           # 包序号
        packet.append(255)         # 包序号补码
        packet.extend(packet_data)
        
        # 计算CRC16
        crc = self._calculate_crc16(packet_data)
        packet.extend(struct.pack('>H', crc))
        
        # 发送包
        self.serial_conn.write(packet)
        
        # 等待ACK
        response = self.serial_conn.read(1)
        return len(response) > 0 and response[0] == YMODEM_ACK
        
    def _send_file_data(self, file_data: bytes, progress_callback: Callable) -> bool:
        """发送文件数据"""
        packet_size = 1024  # 使用1K包
        total_size = len(file_data)
        packet_num = 1
        sent_bytes = 0
        
        while sent_bytes < total_size:
            # 获取数据块
            remaining = total_size - sent_bytes
            chunk_size = min(packet_size, remaining)
            chunk = file_data[sent_bytes:sent_bytes + chunk_size]
            
            # 补齐包大小
            if len(chunk) < packet_size:
                chunk += b'\x1A' * (packet_size - len(chunk))  # 用EOF填充
                
            # 构建数据包
            packet = bytearray()
            packet.append(YMODEM_STX)                    # 1K包头
            packet.append(packet_num & 0xFF)             # 包序号
            packet.append((~packet_num) & 0xFF)          # 包序号补码
            packet.extend(chunk)
            
            # 计算CRC16
            crc = self._calculate_crc16(chunk)
            packet.extend(struct.pack('>H', crc))
            
            # 发送包并等待确认
            retries = 0
            while retries < 5:
                self.serial_conn.write(packet)
                response = self.serial_conn.read(1)
                
                if len(response) > 0 and response[0] == YMODEM_ACK:
                    sent_bytes += chunk_size
                    progress_callback(min(sent_bytes, total_size))
                    packet_num += 1
                    break
                elif len(response) > 0 and response[0] == YMODEM_NAK:
                    retries += 1
                    time.sleep(0.1)
                else:
                    retries += 1
                    time.sleep(0.1)
            else:
                print(f"{Fore.RED}✗{Style.RESET_ALL} Failed to send packet {packet_num}")
                return False
                
        # 发送EOT
        self.serial_conn.write(bytes([YMODEM_EOT]))
        response = self.serial_conn.read(1)
        
        if len(response) > 0 and response[0] == YMODEM_ACK:
            print(f"{Fore.GREEN}✓{Style.RESET_ALL} File transfer completed successfully")
            return True
        else:
            print(f"{Fore.RED}✗{Style.RESET_ALL} Failed to complete transfer")
            return False
            
    def _send_end_packet(self) -> bool:
        """发送结束包"""
        # 空文件名包
        packet_data = b'\x00' * 128
        
        # 构建数据包
        packet = bytearray()
        packet.append(YMODEM_SOH)  # 包头
        packet.append(0)           # 包序号
        packet.append(255)         # 包序号补码
        packet.extend(packet_data)
        
        # 计算CRC16
        crc = self._calculate_crc16(packet_data)
        packet.extend(struct.pack('>H', crc))
        
        # 发送包
        self.serial_conn.write(packet)
        
        # 等待ACK
        response = self.serial_conn.read(1)
        return len(response) > 0 and response[0] == YMODEM_ACK
            
    def _calculate_crc16(self, data: bytes) -> int:
        """计算CRC16校验和"""
        crc = 0
        for byte in data:
            crc ^= byte << 8
            for _ in range(8):
                if crc & 0x8000:
                    crc = (crc << 1) ^ 0x1021
                else:
                    crc <<= 1
                crc &= 0xFFFF
        return crc

def esp32_style_progress_demo():
    """ESP32风格的进度条演示"""
    print(f"{Fore.CYAN}ESP32-style Progress Bar Demo{Style.RESET_ALL}")
    print("=" * 50)
    
    # 模拟擦除阶段
    print(f"\n{Fore.YELLOW}Phase 1: Erasing flash...{Style.RESET_ALL}")
    erase_progress = ProgressBar(100, desc="Erasing", show_speed=False, color="YELLOW")
    for i in range(101):
        erase_progress.update(i)
        time.sleep(0.05)
        
    # 模拟写入阶段
    print(f"\n{Fore.BLUE}Phase 2: Writing firmware...{Style.RESET_ALL}")
    write_progress = ProgressBar(1024*1024, desc="Writing", color="BLUE")  # 1MB
    for i in range(0, 1024*1024, 8192):  # 8KB步长
        write_progress.update(min(i, 1024*1024))
        time.sleep(0.01)
    write_progress.update(1024*1024)
    
    # 模拟验证阶段
    print(f"\n{Fore.GREEN}Phase 3: Verifying...{Style.RESET_ALL}")
    verify_progress = ProgressBar(1024*1024, desc="Verifying", color="GREEN")
    for i in range(0, 1024*1024, 16384):  # 16KB步长
        verify_progress.update(min(i, 1024*1024))
        time.sleep(0.005)
    verify_progress.update(1024*1024)
    
    # 模拟失败情况
    print(f"\n{Fore.RED}Demo: Failed operation{Style.RESET_ALL}")
    fail_progress = ProgressBar(100, desc="Failed", show_speed=False)
    for i in range(60):
        fail_progress.update(i)
        time.sleep(0.02)
    fail_progress.set_failed(True)
    
    print(f"\n{Fore.GREEN}✓ All demos completed successfully!{Style.RESET_ALL}")

def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description='S300 Firmware Download Tool - ESP32 Style',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Download firmware to S300 device
  %(prog)s -p /dev/ttyUSB0 -f firmware.bin
  
  # Download with specific baudrate
  %(prog)s -p COM3 -b 921600 -f app.bin
  
  # Auto-detect serial port and download
  %(prog)s -f firmware.bin
  
  # Demo mode (show progress bars)
  %(prog)s --demo
  
  # OTA update mode
  %(prog)s -p /dev/ttyUSB0 -f firmware.bin --ota
  
  # List available serial ports
  %(prog)s --list-ports
        """
    )
    
    parser.add_argument('-p', '--port', type=str, 
                       help='Serial port (e.g., /dev/ttyUSB0, COM3)')
    parser.add_argument('-b', '--baudrate', type=int, default=115200,
                       help='Baudrate (default: 115200)')
    parser.add_argument('-f', '--file', type=str,
                       help='Firmware file to download')
    parser.add_argument('--ota', action='store_true',
                       help='OTA update mode (vs firmware download)')
    parser.add_argument('--demo', action='store_true',
                       help='Show progress bar demo')
    parser.add_argument('--list-ports', action='store_true',
                       help='List available serial ports')
    
    args = parser.parse_args()
    
    # 列出串口模式
    if args.list_ports:
        ports = list_serial_ports()
        if ports:
            print(f"{Fore.CYAN}Available serial ports:{Style.RESET_ALL}")
            for port in ports:
                desc = getattr(port, 'description', 'Unknown device')
                print(f"  {port.device} - {desc}")
        else:
            print(f"{Fore.YELLOW}No serial ports detected{Style.RESET_ALL}")
        return 0
    
    # Demo模式
    if args.demo:
        esp32_style_progress_demo()
        return 0
        
    # 检查参数
    port = args.port
    if not port:
        port = select_serial_port()
        if not port:
            return 1
            
    if not args.file:
        parser.print_help()
        return 1
        
    if not os.path.exists(args.file):
        print(f"{Fore.RED}✗{Style.RESET_ALL} File not found: {args.file}")
        return 1
        
    # 执行下载
    print(f"{Fore.CYAN}S300 Firmware Download Tool{Style.RESET_ALL}")
    print("=" * 40)
    
    downloader = S300Downloader(port, args.baudrate)
    
    try:
        # 连接设备
        if not downloader.connect():
            return 1
            
        # 探测设备信息
        print(f"{Fore.CYAN}Device Detection:{Style.RESET_ALL}")
        if downloader.probe_device():
            info = downloader.get_device_info()
            for key, value in info.items():
                print(f"  {key.capitalize()}: {value}")
        print()
            
        # 进入下载模式
        if not downloader.enter_download_mode():
            return 1
            
        # 下载固件
        mode = "OTA" if args.ota else "FIRMWARE"
        print(f"{Fore.YELLOW}Mode: {mode} Download{Style.RESET_ALL}")
            
        success = downloader.send_ymodem_file(args.file)
        
        if success:
            print(f"{Fore.GREEN}✓ Download completed successfully!{Style.RESET_ALL}")
            print(f"{Fore.YELLOW}Device will restart automatically...{Style.RESET_ALL}")
            return 0
        else:
            print(f"{Fore.RED}✗ Download failed{Style.RESET_ALL}")
            return 1
            
    except KeyboardInterrupt:
        print(f"\n{Fore.YELLOW}Download cancelled by user{Style.RESET_ALL}")
        return 1
    finally:
        downloader.disconnect()

if __name__ == '__main__':
    sys.exit(main())
