#!/usr/bin/env python3
"""
总线舵机测试脚本 (Hiwonder Bus Servo Tester)
用于在PC上测试总线舵机通信

使用方法:
    python bus_servo_test.py --port COM19
    python bus_servo_test.py --port COM19 --id 1 --angle 120
"""

import serial
import struct
import time
import argparse
from typing import Optional, Tuple

# 协议常量
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
CMD_OR_MOTOR_MODE_WRITE = 29
CMD_OR_MOTOR_MODE_READ = 30
CMD_LOAD_OR_UNLOAD_WRITE = 31
CMD_LOAD_OR_UNLOAD_READ = 32
CMD_LED_CTRL_WRITE = 33
CMD_LED_CTRL_READ = 34
CMD_LED_ERROR_WRITE = 35
CMD_LED_ERROR_READ = 36


class BusServo:
    """总线舵机控制类"""
    
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 0.5):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser: Optional[serial.Serial] = None
        
    def open(self) -> bool:
        """打开串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout
            )
            print(f"[OK] 串口 {self.port} 打开成功 (波特率: {self.baudrate})")
            return True
        except Exception as e:
            print(f"[ERROR] 打开串口失败: {e}")
            return False
    
    def close(self):
        """关闭串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print(f"[OK] 串口 {self.port} 已关闭")
    
    def _calc_checksum(self, data: bytes) -> int:
        """计算校验和"""
        return (~sum(data)) & 0xFF
    
    def _build_packet(self, servo_id: int, cmd: int, params: bytes = b'') -> bytes:
        """构建数据包"""
        length = len(params) + 3  # 长度 = 参数长度 + 3 (length, cmd, checksum)
        packet = bytes([FRAME_HEADER, FRAME_HEADER, servo_id, length, cmd]) + params
        checksum = self._calc_checksum(packet[2:])  # 从 ID 开始计算
        return packet + bytes([checksum])
    
    def _send_packet(self, packet: bytes) -> bool:
        """发送数据包"""
        if not self.ser or not self.ser.is_open:
            print("[ERROR] 串口未打开")
            return False
        
        # 清空接收缓冲区
        self.ser.reset_input_buffer()
        
        # 发送数据
        self.ser.write(packet)
        self.ser.flush()
        
        print(f"[TX] {packet.hex(' ').upper()}")
        return True
    
    def _recv_packet(self, expected_len: int = 0) -> Optional[bytes]:
        """接收数据包"""
        if not self.ser or not self.ser.is_open:
            return None
        
        # 等待并读取帧头
        start_time = time.time()
        header_count = 0
        
        while time.time() - start_time < self.timeout:
            if self.ser.in_waiting > 0:
                byte = self.ser.read(1)
                if byte[0] == FRAME_HEADER:
                    header_count += 1
                    if header_count == 2:
                        break
                else:
                    header_count = 0
            else:
                time.sleep(0.001)
        
        if header_count != 2:
            print("[RX] 超时，未收到帧头")
            return None
        
        # 读取 ID 和长度
        id_len = self.ser.read(2)
        if len(id_len) != 2:
            print("[RX] 读取 ID/长度失败")
            return None
        
        servo_id = id_len[0]
        length = id_len[1]
        
        # 读取剩余数据 (cmd + params + checksum)
        remaining = length - 1  # 长度包含自身
        data = self.ser.read(remaining)
        if len(data) != remaining:
            print(f"[RX] 数据不完整，期望 {remaining} 字节，收到 {len(data)} 字节")
            return None
        
        # 组装完整数据包
        packet = bytes([FRAME_HEADER, FRAME_HEADER, servo_id, length]) + data
        
        # 校验
        checksum = self._calc_checksum(packet[2:-1])
        if checksum != packet[-1]:
            print(f"[RX] 校验错误: 计算值={checksum:02X}, 收到={packet[-1]:02X}")
            return None
        
        print(f"[RX] {packet.hex(' ').upper()}")
        return packet
    
    def _send_recv(self, servo_id: int, cmd: int, params: bytes = b'') -> Optional[bytes]:
        """发送命令并接收响应"""
        packet = self._build_packet(servo_id, cmd, params)
        if not self._send_packet(packet):
            return None
        
        # 半双工通信：先读取并丢弃自己发送的回环数据
        echo = self.ser.read(len(packet))
        if len(echo) == len(packet):
            print(f"[ECHO] {echo.hex(' ').upper()}")
        
        # 等待一小段时间让舵机处理
        time.sleep(0.005)
        
        return self._recv_packet()
    
    # ========== 命令实现 ==========
    
    def ping(self, servo_id: int = BROADCAST_ID) -> Optional[int]:
        """检测舵机，返回舵机ID"""
        resp = self._send_recv(servo_id, CMD_ID_READ)
        if resp and len(resp) >= 6:
            return resp[5]  # 返回的 ID
        return None
    
    def read_id(self) -> Optional[int]:
        """广播读取舵机ID"""
        return self.ping(BROADCAST_ID)
    
    def write_id(self, old_id: int, new_id: int) -> bool:
        """修改舵机ID"""
        params = bytes([new_id])
        resp = self._send_recv(old_id, CMD_ID_WRITE, params)
        return resp is not None
    
    def move(self, servo_id: int, position: int, time_ms: int) -> bool:
        """
        移动舵机到指定位置
        :param servo_id: 舵机ID
        :param position: 目标位置 (0-1000)
        :param time_ms: 运动时间 (ms)
        """
        position = max(0, min(1000, position))
        time_ms = max(0, min(30000, time_ms))
        
        params = struct.pack('<HH', position, time_ms)
        packet = self._build_packet(servo_id, CMD_MOVE_TIME_WRITE, params)
        return self._send_packet(packet)
    
    def move_angle(self, servo_id: int, angle: float, time_ms: int) -> bool:
        """
        移动舵机到指定角度
        :param servo_id: 舵机ID
        :param angle: 目标角度 (0-240度)
        :param time_ms: 运动时间 (ms)
        """
        position = int(angle * 1000 / 240)
        return self.move(servo_id, position, time_ms)
    
    def read_position(self, servo_id: int) -> Optional[int]:
        """读取当前位置"""
        resp = self._send_recv(servo_id, CMD_POS_READ)
        if resp and len(resp) >= 8:
            position = struct.unpack('<h', resp[5:7])[0]  # 有符号16位
            return position
        return None
    
    def read_angle(self, servo_id: int) -> Optional[float]:
        """读取当前角度"""
        pos = self.read_position(servo_id)
        if pos is not None:
            return pos * 240.0 / 1000.0
        return None
    
    def read_voltage(self, servo_id: int) -> Optional[int]:
        """读取输入电压 (mV)"""
        resp = self._send_recv(servo_id, CMD_VIN_READ)
        if resp and len(resp) >= 8:
            voltage = struct.unpack('<H', resp[5:7])[0]
            return voltage
        return None
    
    def read_temperature(self, servo_id: int) -> Optional[int]:
        """读取温度 (°C)"""
        resp = self._send_recv(servo_id, CMD_TEMP_READ)
        if resp and len(resp) >= 6:
            return resp[5]
        return None
    
    def set_load(self, servo_id: int, enable: bool) -> bool:
        """装载/卸载电机"""
        params = bytes([1 if enable else 0])
        packet = self._build_packet(servo_id, CMD_LOAD_OR_UNLOAD_WRITE, params)
        return self._send_packet(packet)
    
    def set_led(self, servo_id: int, off: bool) -> bool:
        """控制LED (off=True 关闭LED)"""
        params = bytes([1 if off else 0])
        packet = self._build_packet(servo_id, CMD_LED_CTRL_WRITE, params)
        return self._send_packet(packet)
    
    def stop(self, servo_id: int) -> bool:
        """停止舵机运动"""
        packet = self._build_packet(servo_id, CMD_MOVE_STOP)
        return self._send_packet(packet)


def main():
    parser = argparse.ArgumentParser(description='总线舵机测试工具')
    parser.add_argument('--port', '-p', type=str, default='COM19', help='串口端口 (默认: COM19)')
    parser.add_argument('--baud', '-b', type=int, default=115200, help='波特率 (默认: 115200)')
    parser.add_argument('--id', '-i', type=int, default=1, help='舵机ID (默认: 1)')
    parser.add_argument('--angle', '-a', type=float, help='目标角度 (0-240)')
    parser.add_argument('--time', '-t', type=int, default=1000, help='运动时间ms (默认: 1000)')
    parser.add_argument('--detect', '-d', action='store_true', help='检测舵机ID')
    parser.add_argument('--status', '-s', action='store_true', help='读取舵机状态')
    parser.add_argument('--interactive', '-I', action='store_true', help='交互模式')
    
    args = parser.parse_args()
    
    servo = BusServo(args.port, args.baud)
    
    if not servo.open():
        return 1
    
    try:
        if args.detect:
            # 检测模式
            print("\n=== 检测舵机 ===")
            detected_id = servo.read_id()
            if detected_id is not None:
                print(f"[OK] 检测到舵机，ID = {detected_id}")
            else:
                print("[FAIL] 未检测到舵机")
        
        elif args.status:
            # 读取状态
            print(f"\n=== 读取舵机 {args.id} 状态 ===")
            
            pos = servo.read_position(args.id)
            if pos is not None:
                angle = pos * 240.0 / 1000.0
                print(f"位置: {pos} (角度: {angle:.1f}°)")
            else:
                print("位置: 读取失败")
            
            voltage = servo.read_voltage(args.id)
            if voltage is not None:
                print(f"电压: {voltage} mV")
            else:
                print("电压: 读取失败")
            
            temp = servo.read_temperature(args.id)
            if temp is not None:
                print(f"温度: {temp} °C")
            else:
                print("温度: 读取失败")
        
        elif args.angle is not None:
            # 移动到指定角度
            print(f"\n=== 移动舵机 {args.id} 到 {args.angle}° (时间: {args.time}ms) ===")
            if servo.move_angle(args.id, args.angle, args.time):
                print("[OK] 命令已发送")
                time.sleep(args.time / 1000.0 + 0.5)
                
                # 读取实际位置
                actual = servo.read_angle(args.id)
                if actual is not None:
                    print(f"[OK] 实际角度: {actual:.1f}°")
            else:
                print("[FAIL] 发送失败")
        
        elif args.interactive:
            # 交互模式
            print("\n=== 交互模式 ===")
            print("命令: d=检测, m=移动, s=状态, l=LED, q=退出")
            print("移动格式: m <角度> [时间ms]")
            
            while True:
                try:
                    cmd = input("\n> ").strip().lower()
                    
                    if cmd == 'q':
                        break
                    elif cmd == 'd':
                        detected_id = servo.read_id()
                        if detected_id is not None:
                            print(f"检测到舵机 ID = {detected_id}")
                        else:
                            print("未检测到舵机")
                    elif cmd == 's':
                        pos = servo.read_position(args.id)
                        voltage = servo.read_voltage(args.id)
                        temp = servo.read_temperature(args.id)
                        print(f"位置: {pos}, 电压: {voltage}mV, 温度: {temp}°C")
                    elif cmd.startswith('m '):
                        parts = cmd.split()
                        angle = float(parts[1])
                        time_ms = int(parts[2]) if len(parts) > 2 else 1000
                        servo.move_angle(args.id, angle, time_ms)
                        print(f"移动到 {angle}° ({time_ms}ms)")
                    elif cmd == 'l0':
                        servo.set_led(args.id, False)
                        print("LED 开")
                    elif cmd == 'l1':
                        servo.set_led(args.id, True)
                        print("LED 关")
                    else:
                        print("未知命令")
                except KeyboardInterrupt:
                    break
                except Exception as e:
                    print(f"错误: {e}")
        
        else:
            # 默认：简单测试
            print("\n=== 简单测试 ===")
            print("1. 检测舵机...")
            detected_id = servo.read_id()
            if detected_id is not None:
                print(f"   检测到舵机 ID = {detected_id}")
                args.id = detected_id
            else:
                print(f"   未检测到舵机，使用默认ID = {args.id}")
            
            print(f"\n2. 移动舵机到 120° ...")
            servo.move_angle(args.id, 120, 1000)
            time.sleep(1.5)
            
            print(f"\n3. 移动舵机到 60° ...")
            servo.move_angle(args.id, 60, 1000)
            time.sleep(1.5)
            
            print(f"\n4. 移动舵机到 180° ...")
            servo.move_angle(args.id, 180, 1000)
            time.sleep(1.5)
            
            print(f"\n5. 回到中间位置 120° ...")
            servo.move_angle(args.id, 120, 1000)
            time.sleep(1.5)
            
            print("\n[OK] 测试完成")
    
    finally:
        servo.close()
    
    return 0


if __name__ == '__main__':
    exit(main())
