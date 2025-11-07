# Build Instructions for RETROS-BIOS

## Quick Start

```bash
# For Raspberry Pi 0 or 1 (default)
make

# For Raspberry Pi 2
make bcm2836

# For Raspberry Pi 3
make bcm2837

# Clean build artifacts
make clean
```

## Detailed Build Instructions

### 1. Install Prerequisites

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install gcc-arm-none-eabi binutils-arm-none-eabi make
```

#### macOS (with Homebrew)
```bash
brew tap ArmMbed/homebrew-formulae
brew install arm-none-eabi-gcc
```

#### Windows (with MSYS2)
```bash
pacman -S mingw-w64-x86_64-arm-none-eabi-gcc
pacman -S mingw-w64-x86_64-arm-none-eabi-binutils
pacman -S make
```

### 2. Clone the Repository

```bash
git clone https://github.com/RobCo-Industries/RETROS-BIOS.git
cd RETROS-BIOS
```

### 3. Build for Your Target

The build system supports three target platforms:

- **BCM2835**: Raspberry Pi Zero, Zero W, 1A, 1B, 1B+
- **BCM2836**: Raspberry Pi 2B
- **BCM2837**: Raspberry Pi 3B, 3B+

Choose the appropriate build command:

```bash
# Default build (BCM2835)
make

# Or explicitly specify target
make TARGET=BCM2835  # RPi0/1
make TARGET=BCM2836  # RPi2
make TARGET=BCM2837  # RPi3
```

### 4. Verify Build Output

After successful build, you should see:

```
=====================================
Build complete: build/kernel.img
Target: BCM2835
Size: 12504 bytes
=====================================
```

The following files are generated in the `build/` directory:
- `kernel.img` - Binary kernel image (copy this to SD card)
- `kernel.elf` - ELF format with debug symbols
- `kernel.list` - Disassembly listing for debugging

### 5. Prepare SD Card

#### Download Raspberry Pi Firmware

Get the bootloader files from the official Raspberry Pi firmware repository:

```bash
wget https://github.com/raspberrypi/firmware/raw/master/boot/bootcode.bin
wget https://github.com/raspberrypi/firmware/raw/master/boot/start.elf
wget https://github.com/raspberrypi/firmware/raw/master/boot/fixup.dat
```

#### Format SD Card

1. Format SD card as FAT32
2. Copy firmware files to SD card:
   - `bootcode.bin`
   - `start.elf`
   - `fixup.dat`
3. Copy `build/kernel.img` to SD card
4. (Optional) Create `config.txt` with your settings

#### Example config.txt

```ini
# Force HDMI output
hdmi_force_hotplug=1
hdmi_drive=2

# Set to 640x480 (mode 4 in group 2)
hdmi_group=2
hdmi_mode=4

# Enable UART for debugging
enable_uart=1

# Disable overscan
disable_overscan=1
```

### 6. Boot Your Raspberry Pi

1. Insert the SD card into your Raspberry Pi
2. Connect HDMI cable to monitor
3. (Optional) Connect UART serial adapter for debugging
4. (Optional) Connect speaker to GPIO 40 for boot beep
5. Power on the Raspberry Pi

You should see:
- Boot beep (if speaker connected)
- RETROS-BIOS splash screen
- Animated boot messages
- Memory test patterns
- Green scanline effect

## Troubleshooting

### Build Errors

**Error: `arm-none-eabi-gcc: command not found`**
- Solution: Install ARM toolchain (see step 1)

**Error: `undefined reference to '__aeabi_uidiv'`**
- Solution: Make sure LIBGCC is being linked (already configured in Makefile)

**Error: `linker.ld: No such file or directory`**
- Solution: Make sure you're running `make` from the repository root

### Runtime Issues

**Black screen on boot**
- Check HDMI cable is connected
- Try adding `hdmi_force_hotplug=1` to config.txt
- Verify kernel.img is on the SD card

**No boot beep**
- Boot beep requires a speaker connected to GPIO 40
- Check PWM is enabled (no config.txt changes needed)

**No UART output**
- Add `enable_uart=1` to config.txt
- Check TX/RX pins are correctly connected
- Verify baud rate is 115200

## Cross-Compilation Details

The build uses the following compiler flags:

### BCM2835 (RPi0/1)
```
-mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard
```

### BCM2836 (RPi2)
```
-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
```

### BCM2837 (RPi3)
```
-mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
```

All builds use:
```
-Wall -Wextra -Werror -O2 -nostdlib -nostartfiles -ffreestanding
```

## Development Tips

### Debugging with UART

Connect a USB-to-TTL adapter:
```
Adapter TX  →  RPi RX (GPIO 15)
Adapter RX  →  RPi TX (GPIO 14)
Adapter GND →  RPi GND
```

Use a terminal program:
```bash
# Linux/macOS
screen /dev/ttyUSB0 115200

# Or minicom
minicom -D /dev/ttyUSB0 -b 115200
```

### Examining the Disassembly

```bash
less build/kernel.list
```

### Checking Symbol Table

```bash
arm-none-eabi-nm build/kernel.elf
```

### Verifying Binary Size

```bash
arm-none-eabi-size build/kernel.elf
```

## Clean Build

To remove all build artifacts:

```bash
make clean
```

This removes the entire `build/` directory.

## Further Reading

- [Raspberry Pi Hardware Documentation](https://www.raspberrypi.org/documentation/)
- [BCM2835 ARM Peripherals](https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf)
- [OSDev Wiki](https://wiki.osdev.org/)
- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/)

## Support

For issues or questions:
- Open an issue on GitHub
- Check existing issues for solutions
- Review the README.md for detailed documentation
