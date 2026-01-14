#!/usr/bin/env python3
"""
S300软件复位控制工具
用于通过串口、网络等方式触发S300设备的软件复位功能
"""

import sys
import time
import argparse
import serial
import requests
import json
from typing import Optional, Dict, Any

class S300SoftwareResetTool:
    """S300软件复位控制工具类"""
    
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
            print(f"✓ 串口连接成功: {port} @ {baudrate}")
            return True
        except Exception as e:
            print(f"✗ 串口连接失败: {e}")
            return False
            
    def disconnect_serial(self):
        """断开串口连接"""
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            print("✓ 串口已断开")
            
    def send_serial_command(self, command: str) -> str:
        """发送串口命令"""
        if not self.serial_port or not self.serial_port.is_open:
            print("✗ 串口未连接")
            return ""

        try:
            # 发送命令
            cmd_bytes = (command + "\r\n").encode('utf-8')
            self.serial_port.write(cmd_bytes)
            print(f"→ 发送命令: {command}")

            # 读取响应
            response = ""
            start_time = time.time()
            while time.time() - start_time < 3.0:  # 3秒超时
                if self.serial_port.in_waiting > 0:
                    data = self.serial_port.read(self.serial_port.in_waiting)
                    response += data.decode('utf-8', errors='ignore')
                    if '\n' in response:
                        break
                time.sleep(0.1)
            print(f"← 设备响应: {response.strip()}")
            return response.strip()
        except Exception as e:
            print(f"✗ 串口通信失败: {e}")
            return ""

    def send_trigger_pattern(self, pattern: str = "spaces", duration: float = 1.0) -> bool:
        """发送触发RBL下载窗口的串口模式
        pattern: 'spaces' 发送连续空格; 'download' 发送"DOWNLOAD"; 'plus' 发送"+++"
        duration: 模式持续时间（秒），仅对'spaces'有意义
        """
        if not self.serial_port or not self.serial_port.is_open:
            print("✗ 串口未连接")
            return False

        try:
            if pattern == "spaces":
                end_time = time.time() + max(0.2, duration)
                payload = b" " * 32  # 一次发送32个空格
                while time.time() < end_time:
                    self.serial_port.write(payload)
                    self.serial_port.flush()
                    time.sleep(0.02)
                print("→ 已发送空格触发序列")
                return True
            elif pattern == "download":
                self.serial_port.write(b"DOWNLOAD\r\n")
                self.serial_port.flush()
                print("→ 已发送DOWNLOAD触发")
                return True
            elif pattern == "plus":
                self.serial_port.write(b"+++\r\n")
                self.serial_port.flush()
                print("→ 已发送+++触发")
                return True
            else:
                print(f"✗ 未知pattern: {pattern}")
                return False
        except Exception as e:
            print(f"✗ 发送触发失败: {e}")
            return False

        try:
            # 发送命令
            command_bytes = f"{command}\r\n".encode('utf-8')
            self.serial_port.write(command_bytes)
            print(f"→ 发送命令: {command}")
            
            # 读取响应
            response = ""
            start_time = time.time()
            while time.time() - start_time < 3.0:  # 3秒超时
                if self.serial_port.in_waiting > 0:
                    data = self.serial_port.read(self.serial_port.in_waiting)
                    response += data.decode('utf-8', errors='ignore')
                    if '\n' in response:
                        break
                time.sleep(0.1)
                
            print(f"← 设备响应: {response.strip()}")
            return response.strip()
            
        except Exception as e:
            print(f"✗ 串口通信失败: {e}")
            return ""
            
    def serial_reset(self, mode: str = "normal") -> bool:
        """通过串口触发复位"""
        if mode == "normal":
            command = "reset"
        elif mode == "download":
            command = "download"
        elif mode == "bootloader":
            command = "bootloader"
        elif mode == "dfu":
            command = "dfu"
        else:
            print(f"✗ 不支持的复位模式: {mode}")
            return False
            
        response = self.send_serial_command(command)
        return "reset" in response.lower() or "download" in response.lower()
        
    def serial_get_status(self) -> Dict[str, Any]:
        """通过串口获取状态"""
        response = self.send_serial_command("status")
        
        # 解析状态信息（简单实现）
        status = {
            "connected": bool(response),
            "response": response
        }
        
        return status
        
    def http_reset(self, ip: str, mode: str = "normal") -> bool:
        """通过HTTP API触发复位"""
        try:
            if mode == "normal":
                action = "reset"
            elif mode == "download":
                action = "download"
            else:
                print(f"✗ 不支持的复位模式: {mode}")
                return False
                
            url = f"http://{ip}/api/system"
            payload = {"action": action}
            
            print(f"→ 发送HTTP请求: {url}")
            print(f"  数据: {payload}")
            
            response = requests.post(url, json=payload, timeout=10)
            
            if response.status_code == 200:
                print(f"✓ HTTP复位成功: {response.text}")
                return True
            else:
                print(f"✗ HTTP复位失败: {response.status_code} {response.text}")
                return False
                
        except Exception as e:
            print(f"✗ HTTP请求失败: {e}")
            return False
            
    def http_get_status(self, ip: str) -> Dict[str, Any]:
        """通过HTTP API获取状态"""
        try:
            url = f"http://{ip}/api/system"
            payload = {"action": "status"}
            
            response = requests.post(url, json=payload, timeout=10)
            
            if response.status_code == 200:
                try:
                    return json.loads(response.text)
                except Exception:
                    return {"raw_response": response.text}
            else:
                return {"error": f"HTTP {response.status_code}"}
                
        except Exception as e:
            return {"error": str(e)}
            
    def detect_device(self, port: str) -> Dict[str, Any]:
        """检测设备并获取信息"""
        print(f"正在检测设备: {port}")
        
        if not self.connect_serial(port):
            return {"detected": False, "error": "连接失败"}
            
        try:
            # 发送检测命令
            info = {}
            
            # 获取状态
            status_response = self.send_serial_command("status")
            if status_response:
                info["status_available"] = True
                info["status_response"] = status_response
            
            # 尝试获取版本信息
            version_response = self.send_serial_command("version")
            if version_response:
                info["version"] = version_response
                
            # 尝试获取芯片信息
            chip_response = self.send_serial_command("chip_info")
            if chip_response:
                info["chip_info"] = chip_response
                
            self.disconnect_serial()
            
            if info:
                info["detected"] = True
                info["port"] = port
                return info
            else:
                return {"detected": False, "error": "无响应"}
                
        except Exception as e:
            self.disconnect_serial()
            return {"detected": False, "error": str(e)}
            
    def scan_ports(self) -> list:
        """扫描可用串口"""
        import serial.tools.list_ports
        
        print("扫描可用串口...")
        ports = []
        
        for port_info in serial.tools.list_ports.comports():
            port_name = port_info.device
            print(f"发现串口: {port_name} - {port_info.description}")
            
            # 尝试检测S300设备
            device_info = self.detect_device(port_name)
            if device_info.get("detected", False):
                print(f"✓ S300设备检测成功: {port_name}")
                device_info["port"] = port_name
                ports.append(device_info)
            else:
                print(f"  设备检测失败: {device_info.get('error', '未知错误')}")
                
        return ports
        
    def double_reset_simulation(self, port: str, interval: float = 1.0) -> bool:
        """模拟双重启操作"""
        print(f"模拟双重启操作，间隔: {interval}秒")
        
        if not self.connect_serial(port):
            return False
            
        try:
            # 第一次复位
            print("第一次复位...")
            self.send_serial_command("reset")
            time.sleep(0.5)  # 等待设备重启
            
            # 等待指定间隔
            print(f"等待 {interval} 秒...")
            time.sleep(interval)
            
            # 重新连接
            self.disconnect_serial()
            time.sleep(0.5)
            
            if not self.connect_serial(port):
                print("✗ 重新连接失败")
                return False
                
            # 第二次复位
            print("第二次复位...")
            self.send_serial_command("reset")
            
            print("✓ 双重启操作完成")
            return True
            
        except Exception as e:
            print(f"✗ 双重启操作失败: {e}")
            return False
        finally:
            self.disconnect_serial()

def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description="S300软件复位控制工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:
  # 扫描设备
  python3 s300_reset_tool.py scan
  
  # 串口复位
  python3 s300_reset_tool.py serial /dev/ttyUSB0 --mode download
  
  # HTTP复位
  python3 s300_reset_tool.py http 192.168.1.100 --mode normal
  
  # 获取状态
  python3 s300_reset_tool.py status /dev/ttyUSB0
  
  # 双重启模拟
  python3 s300_reset_tool.py double-reset /dev/ttyUSB0 --interval 1.5
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', help='可用命令')
    
    # 扫描命令
    scan_parser = subparsers.add_parser('scan', help='扫描可用设备')
    
    # 串口命令
    serial_parser = subparsers.add_parser('serial', help='串口复位')
    serial_parser.add_argument('port', help='串口设备 (如: /dev/ttyUSB0)')
    serial_parser.add_argument('--mode', choices=['normal', 'download', 'bootloader', 'dfu'],
                              default='normal', help='复位模式')
    serial_parser.add_argument('--baudrate', type=int, default=115200, help='波特率')
    
    # HTTP命令
    http_parser = subparsers.add_parser('http', help='HTTP复位')
    http_parser.add_argument('ip', help='设备IP地址')
    http_parser.add_argument('--mode', choices=['normal', 'download'],
                            default='normal', help='复位模式')
    
    # 状态命令
    status_parser = subparsers.add_parser('status', help='获取设备状态')
    status_parser.add_argument('target', help='串口设备或IP地址')
    
    # 双重启命令
    double_parser = subparsers.add_parser('double-reset', help='双重启模拟')
    double_parser.add_argument('port', help='串口设备')
    double_parser.add_argument('--interval', type=float, default=1.0, 
                              help='两次复位间隔时间(秒)')

    # 触发RBL窗口命令
    trigger_parser = subparsers.add_parser('trigger', help='触发RBL下载窗口')
    trigger_parser.add_argument('port', help='串口设备 (如: /dev/ttyUSB0)')
    trigger_parser.add_argument(
        '--pattern', choices=['spaces', 'download', 'plus'], default='spaces')
    trigger_parser.add_argument(
        '--duration', type=float, default=1.2, help='spaces持续时间(秒)')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
        
    tool = S300SoftwareResetTool()
    
    try:
        if args.command == 'scan':
            devices = tool.scan_ports()
            if devices:
                print(f"\n发现 {len(devices)} 个S300设备:")
                for i, device in enumerate(devices, 1):
                    print(f"{i}. {device['port']}")
                    if 'version' in device:
                        print(f"   版本: {device['version']}")
                    if 'chip_info' in device:
                        print(f"   芯片: {device['chip_info']}")
            else:
                print("未发现S300设备")
                
        elif args.command == 'serial':
            if tool.connect_serial(args.port, args.baudrate):
                success = tool.serial_reset(args.mode)
                if success:
                    print(f"✓ 串口复位成功 ({args.mode})")
                    return 0
                else:
                    print(f"✗ 串口复位失败")
                    return 1
                    
        elif args.command == 'http':
            success = tool.http_reset(args.ip, args.mode)
            if success:
                print(f"✓ HTTP复位成功 ({args.mode})")
                return 0
            else:
                print(f"✗ HTTP复位失败")
                return 1
                
        elif args.command == 'status':
            # 判断是串口还是IP
            if '/' in args.target or args.target.startswith('COM'):
                # 串口
                if tool.connect_serial(args.target):
                    status = tool.serial_get_status()
                    print("设备状态:")
                    print(json.dumps(status, indent=2, ensure_ascii=False))
            else:
                # IP地址
                status = tool.http_get_status(args.target)
                print("设备状态:")
                print(json.dumps(status, indent=2, ensure_ascii=False))
                
        elif args.command == 'double-reset':
            success = tool.double_reset_simulation(args.port, args.interval)
            if success:
                print("✓ 双重启模拟完成")
                return 0
            else:
                print("✗ 双重启模拟失败")
                return 1
        elif args.command == 'trigger':
            if tool.connect_serial(args.port):
                ok = tool.send_trigger_pattern(args.pattern, args.duration)
                tool.disconnect_serial()
                return 0 if ok else 1
                
    except KeyboardInterrupt:
        print("\n用户取消操作")
        return 1
    except Exception as e:
        print(f"✗ 执行失败: {e}")
        return 1
    finally:
        tool.disconnect_serial()
        
    return 0

if __name__ == "__main__":
    sys.exit(main())
