#!/usr/bin/env python3
import time
from bus_servo_test import BusServo

def main():
    port = "COM14"
    print(f"正在打开串口 {port}...")
    servo = BusServo(port)
    
    if not servo.open():
        print(f"无法打开串口 {port}")
        print("请检查：")
        print("1. 串口是否被其他程序（如 Bus Servo GUI）占用")
        print("2. 串口号是否正确")
        return

    try:
        print("\n正在读取 1-6 号舵机位置...")
        print("-" * 50)
        print(f"{'ID':<5} {'位置(0-1000)':<15} {'角度(0-240)':<15} {'状态':<10}")
        print("-" * 50)
        
        for servo_id in range(1, 7):
            # 尝试读取位置
            pos = servo.read_position(servo_id)
            
            if pos is not None:
                angle = pos * 240.0 / 1000.0
                print(f"{servo_id:<5} {pos:<15} {angle:<15.1f} {'OK':<10}")
            else:
                print(f"{servo_id:<5} {'---':<15} {'---':<15} {'超时':<10}")
            
            # 稍微延时，避免总线冲突
            time.sleep(0.05)
            
        print("-" * 50)
        print("读取完成")

    finally:
        servo.close()

if __name__ == "__main__":
    main()
