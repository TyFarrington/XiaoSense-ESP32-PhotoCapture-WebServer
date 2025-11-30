# PlatformIO Setup Guide

## Quick Start

1. Open project folder in VS Code with PlatformIO extension
2. PlatformIO auto-detects `platformio.ini`
3. Build: Click checkmark icon
4. Upload: Click arrow icon
5. Monitor: Click plug icon (115200 baud)

## CLI Commands

```bash
cd "data capture"
pio run --target upload
pio device monitor
```

## Configuration

Edit `src/main.cpp` for WiFi credentials.

## Settings (platformio.ini)

- Board: `seeed_xiao_esp32s3`
- PSRAM: Enabled (OPI)
- Upload: 921600 baud
- Monitor: 115200 baud

## Troubleshooting

- **Upload fails**: Press BOOT button during upload
- **No serial output**: Check baud rate (115200), press RESET
- **Camera issues**: Verify camera module connections and power
- **SD card issues**: Ensure SD card is formatted as FAT32
