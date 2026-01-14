#!/usr/bin/env python3
"""
S300 串口监控工具
- 扫描可用串口
- 读取并显示串口输出（带时间戳，可选原样/十六进制显示）
- 可写入日志文件
- 可自动重连

用法示例（uv环境）：
  uv run s300-monitor scan
    uv run s300-monitor read /dev/ttyACM0 \
        --baud 115200 --reconnect --log console.log
  uv run s300-monitor read /dev/ttyACM0 --hex
"""

import sys
import time
import argparse
from datetime import datetime
from typing import Optional

import serial
import serial.tools.list_ports


def list_ports() -> None:
    print("扫描可用串口...")
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("未发现串口设备")
        return
    for p in ports:
        print(f"- {p.device} - {p.description}")


def open_serial(
    port: str,
    baudrate: int,
    timeout: float = 0.1,
) -> Optional[serial.Serial]:
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            timeout=timeout,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        print(f"✓ 串口连接成功: {port} @ {baudrate}")
        return ser
    except Exception as e:
        print(f"✗ 串口连接失败: {e}")
        return None


def hex_dump(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def monitor_serial(
    port: str,
    baudrate: int = 115200,
    show_ts: bool = True,
    hex_mode: bool = False,
    log_path: Optional[str] = None,
    reconnect: bool = False,
) -> int:
    log_file = None
    try:
        if log_path:
            log_file = open(log_path, "a", buffering=1, encoding="utf-8")
            print(f"✓ 日志写入: {log_path}")

        while True:
            ser = open_serial(port, baudrate)
            if not ser:
                if reconnect:
                    time.sleep(1.0)
                    continue
                else:
                    return 1

            try:
                print("--- 按 Ctrl-C 退出 ---")
                while True:
                    if ser.in_waiting:
                        buf = ser.read(ser.in_waiting)
                        if hex_mode:
                            line = hex_dump(buf)
                        else:
                            line = buf.decode("utf-8", errors="ignore")
                        if show_ts:
                            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                            for ln in line.splitlines(True):  # 保留换行
                                out = f"[{ts}] {ln}"
                                print(out, end="")
                                if log_file:
                                    try:
                                        log_file.write(out)
                                    except Exception:
                                        pass
                        else:
                            print(line, end="")
                            if log_file:
                                try:
                                    log_file.write(line)
                                except Exception:
                                    pass
                    else:
                        time.sleep(0.02)
            except KeyboardInterrupt:
                print("\n用户中断，退出")
                return 0
            except Exception as e:
                print(f"\n✗ 读取异常: {e}")
            finally:
                try:
                    ser.close()
                except Exception:
                    pass

            if reconnect:
                print("尝试自动重连...")
                time.sleep(1.0)
                continue
            else:
                break
        return 0
    finally:
        if log_file:
            try:
                log_file.close()
            except Exception:
                pass


def make_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="S300 串口监控工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="command")

    sp_scan = sub.add_parser("scan", help="扫描可用串口")
    sp_scan.set_defaults(func=lambda args: list_ports() or 0)

    sp_read = sub.add_parser("read", help="读取串口输出")
    sp_read.add_argument("port", help="串口设备 (如: /dev/ttyACM0)")
    sp_read.add_argument("--baud", dest="baud", type=int, default=115200,
                         help="波特率")
    sp_read.add_argument("--no-ts", dest="no_ts", action="store_true",
                         help="不显示时间戳")
    sp_read.add_argument("--hex", dest="hex_mode", action="store_true",
                         help="十六进制显示原始数据")
    sp_read.add_argument("--log", dest="log_path", help="将输出写入日志文件")
    sp_read.add_argument("--reconnect", action="store_true", help="断开后自动重连")

    def _run_read(args) -> int:
        return monitor_serial(
            port=args.port,
            baudrate=args.baud,
            show_ts=not args.no_ts,
            hex_mode=args.hex_mode,
            log_path=args.log_path,
            reconnect=args.reconnect,
        )

    sp_read.set_defaults(func=_run_read)
    return p


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    if not getattr(args, "command", None):
        parser.print_help()
        return 1
    try:
        ret = args.func(args)
        return int(ret) if isinstance(ret, int) else 0
    except KeyboardInterrupt:
        print("\n用户中断，退出")
        return 0


if __name__ == "__main__":
    sys.exit(main())
