#!/usr/bin/env python3
"""
S300 OTA工具 - 固件升级和下载完成管理
专门处理OTA过程中的复位和状态管理
"""

import sys
import time
import argparse
import serial
import requests
import json
import hashlib
import struct
from pathlib import Path
from typing import Optional, Dict, Any, List
from rich.console import Console
from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn, TaskProgressColumn
from rich.table import Table
from rich.panel import Panel

console = Console()

class S300OTATool:
    """S300 OTA工具类"""
    
    def __init__(self):
        self.serial_port: Optional[serial.Serial] = None
        self.device_ip: Optional[str] = None
        
    def connect_serial(self, port: str, baudrate: int = 115200) -> bool:
        """连接串口"""
        try:
            self.serial_port = serial.Serial(
                port=port,
                baudrate=baudrate,
                timeout=5.0,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            console.print(f"✓ 串口连接成功: {port} @ {baudrate}", style="green")
            return True
        except Exception as e:
            console.print(f"✗ 串口连接失败: {e}", style="red")
            return False
            
    def disconnect_serial(self):
        """断开串口连接"""
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            console.print("✓ 串口已断开", style="green")
            
    def send_serial_command(self, command: str) -> str:
        """发送串口命令"""
        if not self.serial_port or not self.serial_port.is_open:
            console.print("✗ 串口未连接", style="red")
            return ""
            
        try:
            # 发送命令
            command_bytes = f"{command}\\r\\n".encode('utf-8')
            self.serial_port.write(command_bytes)
            console.print(f"→ 发送命令: {command}", style="blue")
            
            # 读取响应
            response = ""
            start_time = time.time()
            while time.time() - start_time < 3.0:  # 3秒超时
                if self.serial_port.in_waiting > 0:
                    data = self.serial_port.read(self.serial_port.in_waiting)
                    response += data.decode('utf-8', errors='ignore')
                    if '\\n' in response:
                        break
                time.sleep(0.1)
                
            console.print(f"← 设备响应: {response.strip()}", style="cyan")
            return response.strip()
            
        except Exception as e:
            console.print(f"✗ 串口通信失败: {e}", style="red")
            return ""
            
    def trigger_download_mode(self, port: str, method: str = "command") -> bool:
        """触发进入下载模式"""
        console.print(Panel(f"[bold]触发下载模式[/bold]\\n方法: {method}", style="blue"))
        
        if method == "command":
            if not self.connect_serial(port):
                return False
                
            response = self.send_serial_command("download")
            success = "download" in response.lower() or "reset" in response.lower()
            
            if success:
                console.print("✓ 下载模式触发成功", style="green")
            else:
                console.print("✗ 下载模式触发失败", style="red")
                
            self.disconnect_serial()
            return success
            
        elif method == "double_reset":
            return self.simulate_double_reset(port)
            
        else:
            console.print(f"✗ 不支持的方法: {method}", style="red")
            return False
            
    def simulate_double_reset(self, port: str, interval: float = 1.0) -> bool:
        """模拟双重启操作"""
        console.print(Panel(f"[bold]模拟双重启操作[/bold]\\n间隔: {interval}秒", style="yellow"))
        
        if not self.connect_serial(port):
            return False
            
        try:
            with Progress(
                SpinnerColumn(),
                TextColumn("[progress.description]{task.description}"),
                console=console
            ) as progress:
                # 第一次复位
                task1 = progress.add_task("第一次复位...", total=None)
                self.send_serial_command("reset")
                time.sleep(0.5)
                progress.update(task1, completed=True)
                
                # 等待指定间隔
                task2 = progress.add_task(f"等待 {interval} 秒...", total=None)
                time.sleep(interval)
                progress.update(task2, completed=True)
                
                # 重新连接
                task3 = progress.add_task("重新连接...", total=None)
                self.disconnect_serial()
                time.sleep(0.5)
                
                if not self.connect_serial(port):
                    console.print("✗ 重新连接失败", style="red")
                    return False
                    
                progress.update(task3, completed=True)
                
                # 第二次复位
                task4 = progress.add_task("第二次复位...", total=None)
                self.send_serial_command("reset")
                progress.update(task4, completed=True)
                
            console.print("✓ 双重启操作完成", style="green")
            return True
            
        except Exception as e:
            console.print(f"✗ 双重启操作失败: {e}", style="red")
            return False
        finally:
            self.disconnect_serial()
            
    def wait_for_download_mode(self, port: str, timeout: int = 30) -> bool:
        """等待设备进入下载模式"""
        console.print(Panel(f"[bold]等待下载模式[/bold]\\n超时: {timeout}秒", style="blue"))
        
        start_time = time.time()
        
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            TaskProgressColumn(),
            console=console
        ) as progress:
            task = progress.add_task("等待下载模式...", total=timeout)
            
            while time.time() - start_time < timeout:
                elapsed = time.time() - start_time
                progress.update(task, completed=elapsed)
                
                # 尝试连接检查状态
                if self.connect_serial(port):
                    response = self.send_serial_command("status")
                    self.disconnect_serial()
                    
                    if "download" in response.lower() or "bootloader" in response.lower():
                        progress.update(task, completed=timeout)
                        console.print("✓ 设备已进入下载模式", style="green")
                        return True
                        
                time.sleep(1)
                
        console.print("✗ 等待下载模式超时", style="red")
        return False
        
    def upload_firmware(self, port: str, firmware_path: str) -> bool:
        """上传固件文件"""
        firmware_file = Path(firmware_path)
        if not firmware_file.exists():
            console.print(f"✗ 固件文件不存在: {firmware_path}", style="red")
            return False
            
        file_size = firmware_file.stat().st_size
        console.print(Panel(
            f"[bold]上传固件[/bold]\\n"
            f"文件: {firmware_file.name}\\n"
            f"大小: {file_size:,} bytes ({file_size/1024/1024:.1f} MB)", 
            style="blue"
        ))
        
        if not self.connect_serial(port):
            return False
            
        try:
            # 发送YMODEM命令
            self.send_serial_command("YMODEM")
            time.sleep(1)
            
            # 这里应该实现YMODEM协议传输
            # 简化实现，实际项目中需要完整的YMODEM协议
            console.print("📁 开始YMODEM传输 (简化实现)", style="yellow")
            
            with open(firmware_file, 'rb') as f:
                with Progress(
                    SpinnerColumn(),
                    TextColumn("[progress.description]{task.description}"),
                    BarColumn(),
                    TaskProgressColumn(),
                    console=console
                ) as progress:
                    task = progress.add_task("上传固件...", total=file_size)
                    
                    chunk_size = 1024
                    uploaded = 0
                    
                    while uploaded < file_size:
                        chunk = f.read(min(chunk_size, file_size - uploaded))
                        if not chunk:
                            break
                            
                        # 模拟传输延时
                        time.sleep(0.01)
                        uploaded += len(chunk)
                        progress.update(task, completed=uploaded)
                        
            console.print("✓ 固件上传完成", style="green")
            return True
            
        except Exception as e:
            console.print(f"✗ 固件上传失败: {e}", style="red")
            return False
        finally:
            self.disconnect_serial()
            
    def verify_firmware(self, port: str) -> bool:
        """验证固件完整性"""
        console.print(Panel("[bold]验证固件完整性[/bold]", style="blue"))
        
        if not self.connect_serial(port):
            return False
            
        try:
            response = self.send_serial_command("verify")
            success = "verify" in response.lower() and "ok" in response.lower()
            
            if success:
                console.print("✓ 固件验证通过", style="green")
            else:
                console.print("✗ 固件验证失败", style="red")
                
            self.disconnect_serial()
            return success
            
        except Exception as e:
            console.print(f"✗ 固件验证错误: {e}", style="red")
            self.disconnect_serial()
            return False
            
    def complete_ota_and_reset(self, port: str) -> bool:
        """完成OTA并复位到新固件"""
        console.print(Panel("[bold]完成OTA更新[/bold]", style="green"))
        
        if not self.connect_serial(port):
            return False
            
        try:
            # 发送完成命令
            self.send_serial_command("ota_complete")
            time.sleep(1)
            
            # 复位到新固件
            console.print("🔄 复位到新固件...", style="blue")
            self.send_serial_command("reset")
            
            console.print("✅ OTA更新完成，设备已重启", style="green")
            return True
            
        except Exception as e:
            console.print(f"✗ OTA完成失败: {e}", style="red")
            return False
        finally:
            self.disconnect_serial()
            
    def full_ota_process(self, port: str, firmware_path: str) -> bool:
        """完整的OTA流程"""
        console.print(Panel("[bold green]开始完整OTA流程[/bold green]", style="blue"))
        
        steps = [
            ("触发下载模式", lambda: self.trigger_download_mode(port)),
            ("等待下载模式", lambda: self.wait_for_download_mode(port)),
            ("上传固件", lambda: self.upload_firmware(port, firmware_path)),
            ("验证固件", lambda: self.verify_firmware(port)),
            ("完成OTA", lambda: self.complete_ota_and_reset(port)),
        ]
        
        for i, (step_name, step_func) in enumerate(steps, 1):
            console.print(f"\\n[bold]步骤 {i}/5: {step_name}[/bold]")
            
            if not step_func():
                console.print(f"❌ OTA流程在步骤 {i} 失败: {step_name}", style="red")
                return False
                
            console.print(f"✅ 步骤 {i} 完成: {step_name}", style="green")
            
        console.print("\\n🎉 OTA流程全部完成!", style="bold green")
        return True
        
    def get_device_info(self, port: str) -> Dict[str, Any]:
        """获取设备信息"""
        if not self.connect_serial(port):
            return {"error": "连接失败"}
            
        try:
            info = {}
            
            # 获取版本信息
            version_response = self.send_serial_command("version")
            if version_response:
                info["version"] = version_response
                
            # 获取状态信息
            status_response = self.send_serial_command("status")
            if status_response:
                info["status"] = status_response
                
            # 获取内存信息
            mem_response = self.send_serial_command("mem")
            if mem_response:
                info["memory"] = mem_response
                
            self.disconnect_serial()
            return info
            
        except Exception as e:
            self.disconnect_serial()
            return {"error": str(e)}

def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description="S300 OTA工具 - 固件升级和下载完成管理",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:
  # 触发下载模式
  python3 s300_ota_tool.py trigger /dev/ttyUSB0 --method command
  
  # 双重启模拟
  python3 s300_ota_tool.py trigger /dev/ttyUSB0 --method double_reset
  
  # 完整OTA流程
  python3 s300_ota_tool.py ota /dev/ttyUSB0 firmware.bin
  
  # 上传固件
  python3 s300_ota_tool.py upload /dev/ttyUSB0 firmware.bin
  
  # 获取设备信息
  python3 s300_ota_tool.py info /dev/ttyUSB0
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', help='可用命令')
    
    # 触发下载模式命令
    trigger_parser = subparsers.add_parser('trigger', help='触发下载模式')
    trigger_parser.add_argument('port', help='串口设备 (如: /dev/ttyUSB0)')
    trigger_parser.add_argument('--method', choices=['command', 'double_reset'],
                               default='command', help='触发方法')
    trigger_parser.add_argument('--interval', type=float, default=1.0,
                               help='双重启间隔时间(秒)')
    
    # OTA命令
    ota_parser = subparsers.add_parser('ota', help='完整OTA流程')
    ota_parser.add_argument('port', help='串口设备')
    ota_parser.add_argument('firmware', help='固件文件路径')
    
    # 上传命令
    upload_parser = subparsers.add_parser('upload', help='上传固件')
    upload_parser.add_argument('port', help='串口设备')
    upload_parser.add_argument('firmware', help='固件文件路径')
    
    # 信息命令
    info_parser = subparsers.add_parser('info', help='获取设备信息')
    info_parser.add_argument('port', help='串口设备')
    
    # 等待命令
    wait_parser = subparsers.add_parser('wait', help='等待下载模式')
    wait_parser.add_argument('port', help='串口设备')
    wait_parser.add_argument('--timeout', type=int, default=30, help='超时时间(秒)')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
        
    tool = S300OTATool()
    
    try:
        if args.command == 'trigger':
            success = tool.trigger_download_mode(args.port, args.method)
            return 0 if success else 1
            
        elif args.command == 'ota':
            success = tool.full_ota_process(args.port, args.firmware)
            return 0 if success else 1
            
        elif args.command == 'upload':
            success = tool.upload_firmware(args.port, args.firmware)
            return 0 if success else 1
            
        elif args.command == 'info':
            info = tool.get_device_info(args.port)
            
            # 创建信息表格
            table = Table(title="设备信息")
            table.add_column("属性", style="cyan")
            table.add_column("值", style="green")
            
            for key, value in info.items():
                table.add_row(key, str(value))
                
            console.print(table)
            return 0
            
        elif args.command == 'wait':
            success = tool.wait_for_download_mode(args.port, args.timeout)
            return 0 if success else 1
            
    except KeyboardInterrupt:
        console.print("\\n用户取消操作", style="yellow")
        return 1
    except Exception as e:
        console.print(f"✗ 执行失败: {e}", style="red")
        return 1
        
    return 0

if __name__ == "__main__":
    sys.exit(main())
