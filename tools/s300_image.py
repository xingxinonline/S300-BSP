#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
S300 ROMBOOT 镜像工具
======================

生成和提取 S300 芯片的 ROMBOOT 启动镜像。

用法:
    # 生成镜像（仅M4）
    python s300_image.py generate -o output.bin --m4 build/App_HelloWorld
    
    # 生成镜像（M4 + DSP）
    python s300_image.py generate -o output.bin --m4 build/App --dsp build/DSP
    
    # 提取镜像
    python s300_image.py extract image.bin -o extracted/

镜像格式:
    Header (256B) + M4 bin (64B对齐) + [M0 bin] + [DSP bins]
"""

import struct
import sys
import os
import glob
import argparse
from pathlib import Path
from typing import Dict, List, Tuple, Optional


def crc32_rom(data: bytes, init: int = 0) -> int:
    """ROM风格CRC32: 非反射, poly=0x04C11DB7"""
    poly = 0x04C11DB7
    table = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            if c & 0x80000000:
                c = ((c << 1) ^ poly) & 0xFFFFFFFF
            else:
                c = (c << 1) & 0xFFFFFFFF
        table.append(c)

    crc = init & 0xFFFFFFFF
    for b in data:
        idx = ((crc >> 24) ^ b) & 0xFF
        crc = ((crc << 8) ^ table[idx]) & 0xFFFFFFFF
    return crc & 0xFFFFFFFF


class ImageGenerator:
    """S300 镜像生成器"""
    
    # DSP RAM区域配置
    DSP_RAM_CONFIG = [
        {'name': 'PTCM',  'ram_addr': 0x44A00000, 'file': 'ptcm_boot.bin'},
        {'name': 'DTCM',  'ram_addr': 0x44800000, 'file': 'dtcm_boot.bin'},
        {'name': 'SRAM0', 'ram_addr': 0x44000000, 'file': 'sram0_boot.bin'},
        {'name': 'SRAM1', 'ram_addr': 0x44040000, 'file': 'sram1_boot.bin'},
        {'name': 'PSRAM', 'ram_addr': 0x80000000, 'file': 'psram_boot.bin'},
    ]
    
    def __init__(self, dsp_pro: Optional[int] = None, clock_config: Optional[Tuple[int, ...]] = None, verbose: bool = False):
        self.header = bytearray(256)
        self.image_data = bytearray()
        # 设置默认的 DSP Pro 字段
        # 0x00E00409: 支持 PSRAM 模型加载（RBL 会先初始化 PSRAM）
        # 0x00E00405: 不支持 PSRAM（仅 SRAM）
        self.dsp_pro = dsp_pro if dsp_pro is not None else 0x00E00409
        # 设置默认的时钟配置（4个参数：REF Clock, FOUT Clock, PLL Config 0, PLL Config 1）
        self.clock_config = clock_config if clock_config is not None else (0x00989680, 0x05F5E100, 0x00154320, 0x05000000)
        self.verbose = verbose
    
    def find_bin_file(self, folder: str, core_type: Optional[str] = None) -> Optional[str]:
        """在文件夹中查找.bin文件（递归搜索）
        
        Args:
            folder: 搜索的文件夹路径或直接的文件路径
            core_type: 核心类型，如 'm4', 'm0'，用于优先匹配特定核心的文件
        """
        if not folder or not os.path.exists(folder):
            return None
            
        # 如果是文件，直接返回
        if os.path.isfile(folder):
            return folder
        
        # 递归搜索所有.bin文件
        bin_files = glob.glob(os.path.join(folder, '**', '*.bin'), recursive=True)
        
        if not bin_files:
            return None
        
        if len(bin_files) == 1:
            return bin_files[0]
        
        # 如果指定了核心类型，优先匹配核心名称
        if core_type:
            core_keywords = {
                'm4': ['m4', 'cortex-m4', 'cortex_m4'],
                'm0': ['m0', 'cortex-m0', 'cortex_m0'],
            }
            keywords = core_keywords.get(core_type.lower(), [])
            for keyword in keywords:
                for bin_file in bin_files:
                    if keyword in bin_file.lower():
                        return bin_file
        
        # 多个bin文件时，优先选择包含特定关键字的
        for keyword in ['out', 'build', 'release', 'output']:
            for bin_file in bin_files:
                if keyword in bin_file.lower():
                    return bin_file
        
        # 没有特殊关键字，返回第一个
        return bin_files[0]
    
    def find_dsp_bins(self, folder: str) -> Dict[str, str]:
        """在DSP文件夹中查找各个RAM区域的bin文件"""
        if not folder or not os.path.exists(folder):
            return {}
        
        dsp_files = {}
        
        # 搜索每个RAM区域的bin文件
        for ram_cfg in self.DSP_RAM_CONFIG:
            ram_name = ram_cfg['name'].lower()
            target_file = ram_cfg['file']
            
            # 模糊匹配：匹配任何以 target_file 结尾的文件（忽略前缀）
            # 例如：匹配 ptcm_boot.bin, model_ptcm_boot.bin, dsp_ptcm_boot.bin 等
            pattern = os.path.join(folder, f'*{target_file}')
            matched = glob.glob(pattern, recursive=False)
            if matched:
                dsp_files[ram_cfg['name']] = matched[0]
                continue
            
            # 递归搜索
            pattern = os.path.join(folder, '**', f'*{target_file}')
            matched = glob.glob(pattern, recursive=True)
            if matched:
                dsp_files[ram_cfg['name']] = matched[0]
        
        return dsp_files
    
    def read_bin_file(self, file_path: str) -> bytes:
        """读取bin文件"""
        if not file_path or not os.path.exists(file_path):
            return b''
        
        with open(file_path, 'rb') as f:
            data = f.read()
        
        return data
    
    def build_cortex_m4_section(self, bin_file: str) -> Tuple[bytes, Dict]:
        """构建Cortex-M4段（必须）"""
        if self.verbose:
            print("\n【构建Cortex-M4段】")
        
        # 读取bin文件
        m4_data = bytearray(self.read_bin_file(bin_file))
        if not m4_data:
            raise ValueError(f"无法读取Cortex-M4 bin文件: {bin_file}")
        
        if self.verbose:
            print(f"[OK] 读取: {bin_file}")
            print(f"  原始大小: {len(m4_data)} bytes")
        original_size = len(m4_data)
        
        # 对齐到64字节边界
        alignment = 64
        remainder = len(m4_data) % alignment
        if remainder != 0:
            padding = alignment - remainder
            m4_data.extend(b'\x00' * padding)
            if self.verbose:
                print(f"  对齐填充: {padding} bytes (对齐后{len(m4_data)} bytes)")
        
        # 计算CRC32
        m4_crc32 = crc32_rom(m4_data, 0)
        
        # 构建Pro字段
        pro = 0x00F34009
        flash_addr = 0x00000100
        exe_addr = 0x20000000
        version = b'CM4 Core\x00\x00\x00\x00'
        
        info = {
            'pro': pro,
            'flash_addr': flash_addr,
            'exe_addr': exe_addr,
            'length': len(m4_data),
            'crc32': m4_crc32,
            'version': version,
            'data': m4_data,
        }
        
        if self.verbose:
            print(f"  Flash地址: 0x{flash_addr:08X}")
            print(f"  执行地址: 0x{exe_addr:08X}")
            print(f"  CRC32: 0x{m4_crc32:08X}")
        
        return bytes(m4_data), info
    
    def build_cortex_m0_section(self, bin_file: Optional[str], flash_offset: int) -> Tuple[bytes, Optional[Dict]]:
        """构建Cortex-M0段（可选）"""
        if not bin_file:
            return b'', None
            
        if self.verbose:
            print("\n【构建Cortex-M0段】")
        
        # 读取bin文件
        m0_data = bytearray(self.read_bin_file(bin_file))
        if not m0_data:
            if self.verbose:
                print("[WARN] 未找到Cortex-M0 bin文件，跳过此段")
            return b'', None
        
        if self.verbose:
            print(f"[OK] 读取: {bin_file}")
            print(f"  大小: {len(m0_data)} bytes")
        
        # M0段通常不需要对齐（如果需要可以添加）
        
        # 计算CRC32
        m0_crc32 = crc32_rom(m0_data, 0)
        
        # 构建Pro字段
        pro = 0x00E06005
        exe_addr = 0x00000000
        version = b'CM0 Core\x00\x00\x00\x00'
        
        info = {
            'pro': pro,
            'flash_addr': flash_offset,
            'exe_addr': exe_addr,
            'length': len(m0_data),
            'crc32': m0_crc32,
            'version': version,
            'data': m0_data,
        }
        
        if self.verbose:
            print(f"  Flash地址: 0x{flash_offset:08X}")
            print(f"  CRC32: 0x{m0_crc32:08X}")
        
        return bytes(m0_data), info
    
    def build_dsp_section(self, dsp_files: Dict[str, str], flash_offset: int) -> Tuple[bytes, Optional[Dict]]:
        """构建DSP段（可选）"""
        if not dsp_files:
            return b'', None
            
        if self.verbose:
            print("\n【构建DSP/CPT段】")
        
        dsp_data = bytearray()
        ram_info = []
        total_size = 0
        
        # 读取各个DSP RAM区域的bin文件
        for ram_cfg in self.DSP_RAM_CONFIG:
            ram_name = ram_cfg['name']
            ram_addr = ram_cfg['ram_addr']
            
            # 从dsp_files中获取对应的bin文件
            bin_file = dsp_files.get(ram_name)
            if not bin_file:
                # 没有此RAM区域的文件，添加空占位
                ram_info.append({
                    'name': ram_name,
                    'flash_addr': flash_offset + len(dsp_data),
                    'ram_addr': ram_addr,
                    'ram_map': 0x00000000,
                    'size': 0,
                    'crc32': 0,
                })
                continue
            
            # 读取bin文件
            data = bytearray(self.read_bin_file(bin_file))
            
            if len(data) > 0:
                if self.verbose:
                    print(f"[OK] 读取: {bin_file}")
                    print(f"  [{ram_name:>6}] 原始大小: {len(data)} bytes")
                original_size = len(data)
                
                # 对齐到64字节边界
                alignment = 64
                remainder = len(data) % alignment
                if remainder != 0:
                    padding = alignment - remainder
                    data.extend(b'\x00' * padding)
                    if self.verbose:
                        print(f"  [{ram_name:>6}] 对齐填充: {padding} bytes (对齐后{len(data)} bytes)")
                
                # 当前RAM段在FLASH中的地址
                ram_flash_addr = flash_offset + len(dsp_data)
                
                # 计算CRC32
                ram_crc32 = crc32_rom(data, 0)
                
                # ram_map 规则：PTCM/DTCM 为 0，SRAM0/SRAM1/PSRAM 等于 ram_addr
                if ram_name in ('SRAM0', 'SRAM1', 'PSRAM'):
                    ram_map_val = ram_addr
                else:
                    ram_map_val = 0x00000000
                
                ram_info.append({
                    'name': ram_name,
                    'flash_addr': ram_flash_addr,
                    'ram_addr': ram_addr,
                    'ram_map': ram_map_val,
                    'size': len(data),
                    'crc32': ram_crc32,
                })
                
                if self.verbose:
                    print(f"  [{ram_name:>6}] Flash=0x{ram_flash_addr:08X}, RAM=0x{ram_addr:08X}, CRC32=0x{ram_crc32:08X}")
                
                # 添加数据
                dsp_data.extend(data)
                total_size += len(data)
            else:
                # 空数据
                ram_info.append({
                    'name': ram_name,
                    'flash_addr': flash_offset + len(dsp_data),
                    'ram_addr': ram_addr,
                    'ram_map': 0x00000000,
                    'size': 0,
                    'crc32': 0,
                })
        
        if total_size == 0:
            return b'', None
        
        # 构建DSP Pro字段
        # 0x00E00409: 支持 PSRAM 模型加载（RBL 会先初始化 PSRAM）
        # 0x00E00405: 不支持 PSRAM（仅 SRAM）
        # 用户可以通过 --dsp-pro 参数自定义
        pro = self.dsp_pro
        if self.verbose:
            print(f"  使用 DSP Pro: 0x{pro:08X}")
        
        dsp_flash_addr = flash_offset
        version = b'DSP Core\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
        
        info = {
            'pro': pro,
            'flash_addr': dsp_flash_addr,
            'exe_addr': 0x00000000,
            'length': total_size,
            'ram_info': ram_info,
            'version': version,
            'data': bytes(dsp_data),
        }
        
        if self.verbose:
            print(f"  DSP总长度: {total_size} bytes")
        
        return bytes(dsp_data), info
    
    def build_header(self, m4_info: Dict, m0_info: Optional[Dict], dsp_info: Optional[Dict]) -> bytearray:
        """构建256字节header"""
        if self.verbose:
            print("\n【构建Header】")
        
        header = bytearray(256)
        
        # === Cortex-M4段 (0x00-0x1F) - 必须 ===
        struct.pack_into('<I', header, 0x00, m4_info['pro'])
        struct.pack_into('<I', header, 0x04, m4_info['flash_addr'])
        struct.pack_into('<I', header, 0x08, m4_info['exe_addr'])
        struct.pack_into('<I', header, 0x0C, m4_info['length'])
        struct.pack_into('<I', header, 0x10, m4_info['crc32'])
        header[0x14:0x20] = m4_info['version']
        
        # === Cortex-M0段 (0x20-0x3F) - 可选 ===
        if m0_info:
            struct.pack_into('<I', header, 0x20, m0_info['pro'])
            struct.pack_into('<I', header, 0x24, m0_info['flash_addr'])
            struct.pack_into('<I', header, 0x28, m0_info['exe_addr'])
            struct.pack_into('<I', header, 0x2C, m0_info['length'])
            struct.pack_into('<I', header, 0x30, m0_info['crc32'])
            header[0x34:0x40] = m0_info['version']
        
        # === DSP/CPT段 (0x40-0xBF) - 可选 ===
        if dsp_info:
            struct.pack_into('<I', header, 0x40, dsp_info['pro'])
            struct.pack_into('<I', header, 0x44, dsp_info['flash_addr'])
            struct.pack_into('<I', header, 0x48, dsp_info['exe_addr'])
            struct.pack_into('<I', header, 0x4C, dsp_info['length'])
            
            # DSP RAM映射信息 (5个RAM区域 * 20字节)
            for i, ram in enumerate(dsp_info['ram_info']):
                offset = 0x50 + i * 20
                struct.pack_into('<I', header, offset + 0,  ram['flash_addr'])
                struct.pack_into('<I', header, offset + 4,  ram['ram_addr'])
                struct.pack_into('<I', header, offset + 8,  ram['ram_map'])
                struct.pack_into('<I', header, offset + 12, ram['size'])
                struct.pack_into('<I', header, offset + 16, ram['crc32'])
            
            # DSP版本信息 (0xB4-0xCB, 24字节)
            header[0xB4:0xCC] = dsp_info['version']
        
        # === Other段 (0xCC-0xEB) - 存储镜像总大小 ===
        # 0xC8 字段 (参考镜像中的值: 0x000000E0)
        struct.pack_into('<I', header, 0xC8, 0x000000E0)  # 可能是某种标志或版本
        
        struct.pack_into('<I', header, 0xCC, 0x00000000)  # Pro
        # 计算总大小 = Header(256) + M4段 + M0段 + DSP段
        total_image_size = 256 + m4_info['length']
        if m0_info:
            total_image_size += m0_info['length']
        if dsp_info:
            total_image_size += dsp_info['length']
        struct.pack_into('<I', header, 0xD0, total_image_size)  # 镜像总大小
        struct.pack_into('<I', header, 0xD4, 0x00000000)  # Exe Addr
        struct.pack_into('<I', header, 0xD8, 0x00000000)  # Len
        struct.pack_into('<I', header, 0xDC, 0x00000000)  # Check
        
        # === 时钟配置 (0xEC-0xFB) ===
        # 默认值: (0x00989680, 0x05F5E100, 0x00154320, 0x05000000)
        # 用户可以通过 --clock-config 参数自定义（2或4个参数）
        if len(self.clock_config) == 2:
            # 仅提供了 0xEC 和 0xF0
            clkconfig0, clkconfig1 = self.clock_config
            if self.verbose:
                print(f"  使用时钟配置: Config0=0x{clkconfig0:08X}, Config1=0x{clkconfig1:08X}")
            
            # 写入配置
            struct.pack_into('<I', header, 0xEC, clkconfig0)
            struct.pack_into('<I', header, 0xF0, clkconfig1)
            struct.pack_into('<I', header, 0xF4, 0x00000000)  # PLL Config 0 清零
            struct.pack_into('<I', header, 0xF8, 0x00000000)  # PLL Config 1 清零
            
        elif len(self.clock_config) == 4:
            # 提供了完整的 4 个时钟配置参数
            clkconfig0, clkconfig1, pllconfig0, pllconfig1 = self.clock_config
            if self.verbose:
                print(f"  使用时钟配置:")
                print(f"    0xEC (Clock 0):    0x{clkconfig0:08X}")
                print(f"    0xF0 (Clock 1):    0x{clkconfig1:08X}")
                print(f"    0xF4 (PLL Config 0): 0x{pllconfig0:08X}")
                print(f"    0xF8 (PLL Config 1): 0x{pllconfig1:08X}")
                
                # 解析 PLL Config 0 显示
                timeout = (pllconfig0 >> 18) & 0xFF
                postdiv2 = (pllconfig0 >> 15) & 0x7
                postdiv1 = (pllconfig0 >> 12) & 0x7
                fbdiv = pllconfig0 & 0xFFF
                print(f"    → PLL: timeout={timeout}, postdiv2={postdiv2}, postdiv1={postdiv1}, fbdiv={fbdiv}")
                
                # 解析 PLL Config 1 显示
                refdiv = (pllconfig1 >> 24) & 0x3F
                print(f"    → PLL: refdiv={refdiv}")
            
            # 写入配置
            struct.pack_into('<I', header, 0xEC, clkconfig0)
            struct.pack_into('<I', header, 0xF0, clkconfig1)
            struct.pack_into('<I', header, 0xF4, pllconfig0)
            struct.pack_into('<I', header, 0xF8, pllconfig1)
        else:
            raise ValueError(f"clock_config 必须是 2 或 4 个参数，当前为 {len(self.clock_config)} 个")
        
        # === Header CRC32 (0xFC) ===
        # 先清零CRC32字段，然后对完整256字节计算CRC32
        header[0xFC:0x100] = b'\x00\x00\x00\x00'
        header_crc32 = crc32_rom(header, 0)
        struct.pack_into('<I', header, 0xFC, header_crc32)
        
        if self.verbose:
            print(f"  Header大小: 256 bytes")
            print(f"  镜像总大小: {total_image_size} bytes ({total_image_size/1024:.2f} KB)")
            print(f"  Header CRC32: 0x{header_crc32:08X}")
        
        return header
    
    def generate_image(self, output_file: str, m4_folder: str, m0_folder: Optional[str] = None, 
                      dsp_folder: Optional[str] = None) -> bool:
        """生成完整镜像"""
        try:
            if self.verbose:
                print("=" * 80)
                print("S300 镜像生成器 V2")
                print("=" * 80)
            
            # 1. 查找M4 bin文件（必须）
            m4_bin = self.find_bin_file(m4_folder, 'm4')
            if not m4_bin:
                raise ValueError(f"在文件夹 {m4_folder} 中未找到 .bin 文件")
            
            # 2. 构建M4段
            m4_data, m4_info = self.build_cortex_m4_section(m4_bin)
            m4_offset = 0x100
            
            # 3. 查找并构建M0段（可选）
            m0_bin = self.find_bin_file(m0_folder, 'm0') if m0_folder else None
            m0_offset = m4_offset + len(m4_data)
            m0_data, m0_info = self.build_cortex_m0_section(m0_bin, m0_offset)
            
            # 4. 查找并构建DSP段（可选）
            dsp_files = self.find_dsp_bins(dsp_folder) if dsp_folder else {}
            dsp_offset = m0_offset + len(m0_data)
            dsp_data, dsp_info = self.build_dsp_section(dsp_files, dsp_offset)
            
            # 5. 构建Header
            header = self.build_header(m4_info, m0_info, dsp_info)
            
            # 6. 组合完整镜像
            if self.verbose:
                print("\n【组合镜像】")
            image = header + m4_data + m0_data + dsp_data
            
            # 7. 写入文件
            output_path = os.path.abspath(output_file)
            with open(output_path, 'wb') as f:
                f.write(image)
            
            if self.verbose:
                print(f"  Header:    256 bytes (0x000-0x0FF)")
                print(f"  M4段:      {len(m4_data)} bytes (0x{m4_offset:X}-0x{m4_offset+len(m4_data)-1:X})")
                if len(m0_data) > 0:
                    print(f"  M0段:      {len(m0_data)} bytes (0x{m0_offset:X}-0x{m0_offset+len(m0_data)-1:X})")
                if len(dsp_data) > 0:
                    print(f"  DSP段:     {len(dsp_data)} bytes (0x{dsp_offset:X}-0x{dsp_offset+len(dsp_data)-1:X})")
                print(f"  总大小:    {len(image)} bytes ({len(image)/1024:.2f} KB)")
                print(f"\n[OK] 镜像生成成功: {output_path}")
                print("=" * 80)
            else:
                print(f"[OK] {output_path} ({len(image)/1024:.2f} KB)")
            
            return True
            
        except Exception as e:
            print(f"\n[ERR] 生成失败: {e}")
            import traceback
            traceback.print_exc()
            return False


class ImageExtractor:
    """S300 镜像提取器"""
    
    DSP_RAM_NAMES = ['PTCM', 'DTCM', 'SRAM0', 'SRAM1', 'PSRAM']
    
    def __init__(self, image_file: str, output_dir: str):
        self.image_file = image_file
        self.output_dir = output_dir
        Path(output_dir).mkdir(parents=True, exist_ok=True)
        
    def extract(self):
        """提取镜像中的所有核心 bin 文件"""
        print("=" * 60)
        print("S300 镜像提取器")
        print("=" * 60)
        print(f"输入: {self.image_file}")
        print(f"输出: {self.output_dir}")
        
        with open(self.image_file, 'rb') as f:
            image_data = f.read()
        
        header = image_data[:256]
        
        self._extract_m4(header, image_data)
        self._extract_m0(header, image_data)
        self._extract_dsp(header, image_data)
        
        print(f"\n[OK] 提取完成: {self.output_dir}")
        
    def _extract_m4(self, header: bytes, image_data: bytes):
        """提取 Cortex-M4"""
        print("\n[M4]")
        flash_addr = struct.unpack_from('<I', header, 0x04)[0]
        length = struct.unpack_from('<I', header, 0x0C)[0]
        crc32 = struct.unpack_from('<I', header, 0x10)[0]
        
        print(f"  Addr: 0x{flash_addr:08X}, Len: {length}, CRC: 0x{crc32:08X}")
        
        data = image_data[flash_addr : flash_addr + length]
        calc_crc = crc32_rom(data, 0)
        print(f"  CRC验证: {'[OK]' if calc_crc == crc32 else '[ERR]'}")
        
        out_file = os.path.join(self.output_dir, "cortex_m4.bin")
        with open(out_file, 'wb') as f:
            f.write(data)
        print(f"  保存: {out_file}")
        
    def _extract_m0(self, header: bytes, image_data: bytes):
        """提取 Cortex-M0"""
        length = struct.unpack_from('<I', header, 0x2C)[0]
        if length == 0:
            return
            
        print("\n[M0]")
        flash_addr = struct.unpack_from('<I', header, 0x24)[0]
        crc32 = struct.unpack_from('<I', header, 0x30)[0]
        
        print(f"  Addr: 0x{flash_addr:08X}, Len: {length}, CRC: 0x{crc32:08X}")
        
        data = image_data[flash_addr : flash_addr + length]
        calc_crc = crc32_rom(data, 0)
        print(f"  CRC验证: {'[OK]' if calc_crc == crc32 else '[ERR]'}")
        
        out_file = os.path.join(self.output_dir, "cortex_m0.bin")
        with open(out_file, 'wb') as f:
            f.write(data)
        print(f"  保存: {out_file}")
        
    def _extract_dsp(self, header: bytes, image_data: bytes):
        """提取 DSP"""
        dsp_length = struct.unpack_from('<I', header, 0x4C)[0]
        if dsp_length == 0:
            return
            
        print("\n[DSP]")
        
        for i, ram_name in enumerate(self.DSP_RAM_NAMES):
            offset = 0x50 + i * 20
            flash_addr = struct.unpack_from('<I', header, offset)[0]
            size = struct.unpack_from('<I', header, offset + 12)[0]
            crc32 = struct.unpack_from('<I', header, offset + 16)[0]
            
            if size == 0:
                continue
            
            print(f"  {ram_name}: Addr=0x{flash_addr:08X}, Size={size}")
            
            data = image_data[flash_addr : flash_addr + size]
            calc_crc = crc32_rom(data, 0)
            print(f"    CRC: {'[OK]' if calc_crc == crc32 else '[ERR]'}")
            
            out_file = os.path.join(self.output_dir, f"dsp_{ram_name.lower()}_boot.bin")
            with open(out_file, 'wb') as f:
                f.write(data)
            print(f"    保存: {out_file}")


def cmd_generate(args):
    """generate子命令"""
    clock_config = None
    if args.clock_config:
        if len(args.clock_config) not in [2, 4]:
            print(f"[ERR] --clock-config 需要 2 或 4 个参数")
            return 1
        clock_config = tuple(args.clock_config)
    
    generator = ImageGenerator(dsp_pro=args.dsp_pro, clock_config=clock_config, verbose=args.verbose)
    
    if generator.generate_image(args.output, args.m4, args.m0, args.dsp):
        return 0
    return 1


def cmd_extract(args):
    """extract子命令"""
    if not os.path.exists(args.image):
        print(f"[ERR] 文件不存在: {args.image}")
        return 1
    
    extractor = ImageExtractor(args.image, args.output)
    extractor.extract()
    return 0


def main():
    parser = argparse.ArgumentParser(
        description='S300 ROMBOOT 镜像工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    subparsers = parser.add_subparsers(dest='command', help='子命令')
    
    # generate 子命令
    gen_parser = subparsers.add_parser('generate', help='生成镜像',
        epilog="""
示例:
  python s300_image.py generate -o out.bin --m4 build/App_HelloWorld
  python s300_image.py generate -o out.bin --m4 build/App --dsp build/DSP
        """)
    gen_parser.add_argument('-o', '--output', required=True, help='输出镜像文件')
    gen_parser.add_argument('--m4', required=True, help='M4 bin文件夹（必须）')
    gen_parser.add_argument('--m0', help='M0 bin文件夹（可选）')
    gen_parser.add_argument('--dsp', help='DSP bin文件夹（可选）')
    gen_parser.add_argument('--dsp-pro', type=lambda x: int(x, 0),
                           help='DSP Pro值（默认: 0x00E00409，支持PSRAM）')
    gen_parser.add_argument('--clock-config', nargs='+', type=lambda x: int(x, 0),
                           help='时钟配置（2或4个十六进制值）')
    gen_parser.add_argument('-v', '--verbose', action='store_true',
                           help='显示详细构建信息')
    
    # extract 子命令
    ext_parser = subparsers.add_parser('extract', help='提取镜像',
        epilog="""
示例:
  python s300_image.py extract image.bin -o extracted/
        """)
    ext_parser.add_argument('image', help='输入镜像文件')
    ext_parser.add_argument('-o', '--output', default='extracted', help='输出目录')
    
    args = parser.parse_args()
    
    if args.command == 'generate':
        sys.exit(cmd_generate(args))
    elif args.command == 'extract':
        sys.exit(cmd_extract(args))
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()
