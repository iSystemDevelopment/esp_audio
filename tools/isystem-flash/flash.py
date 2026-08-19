#!/usr/bin/env python3
"""Flash a local ESP32 .bin with esptool. No network, no keys."""
import sys

def main():
    if len(sys.argv) < 3:
        print("usage: python flash.py <PORT> <firmware.bin>")
        print("example: python flash.py COM3 ..\\..\\.pio\\build\\esp32-s3\\firmware.bin")
        return 2
    port, bin_path = sys.argv[1], sys.argv[2]
    try:
        import esptool
    except ImportError:
        print("Install esptool: pip install esptool")
        return 1
    esptool.main(["--chip", "auto", "--port", port, "--baud", "921600", "write_flash", "0x0", bin_path])
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
