#!/usr/bin/env python3
"""
安装S300下载工具所需的依赖包
"""

import subprocess
import sys

def install_requirements():
    """安装依赖包"""
    requirements = [
        'pyserial>=3.5',
        'colorama>=0.4.4'
    ]
    
    for package in requirements:
        print(f"Installing {package}...")
        try:
            subprocess.check_call([sys.executable, '-m', 'pip', 'install', package])
            print(f"✓ {package} installed successfully")
        except subprocess.CalledProcessError as e:
            print(f"✗ Failed to install {package}: {e}")
            return False
    
    return True

if __name__ == '__main__':
    if install_requirements():
        print("\n✓ All dependencies installed successfully!")
        print("You can now run: python3 s300_download.py --demo")
    else:
        print("\n✗ Some dependencies failed to install")
        sys.exit(1)
