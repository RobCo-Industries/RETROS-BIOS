# RETROS-BIOS

**RobCo Industries™ Terminal BIOS - Fallout-Style Bootloader**

An authentic Fallout-inspired bare-metal bootloader for Raspberry Pi (RPi0, RPi1, and RPi2) written in C.

![RobCo Industries](https://img.shields.io/badge/RobCo-Industries-green)
![Platform](https://img.shields.io/badge/platform-Raspberry%20Pi-red)
![License](https://img.shields.io/badge/license-MIT-blue)

## Features

### Core Functionality
- **Multi-Platform Support**: BCM2835 (RPi0/1), BCM2836 (RPi2), BCM2837 (RPi3)
- **UART Debug Output**: Full serial debugging support at 115200 baud
- **HDMI Framebuffer**: 640x480 32-bit color display
- **8x16 VGA Font**: Authentic terminal-style text rendering
- **SD Card Driver**: Basic SD card support for chain-loading
- **Chain-Loading**: Load and execute next-stage bootloader or kernel

### Fallout Terminal Aesthetics
- **Boot Beep**: PWM audio with authentic three-tone boot sequence
- **Scanline Effect**: CRT-style scanlines on HDMI output
- **Memory Test Patterns**: Random memory test displays for authenticity
- **Bad Sector Warnings**: Occasional simulated warnings (15% chance)
- **Hidden Diagnostics**: Press 'D' during boot for diagnostic mode
- **Typing Animation**: Boot messages appear with typewriter effect
- **Retro Color Scheme**: Green terminal text on black background

## Hardware Requirements

- Raspberry Pi Zero, Zero W, 1A, 1B, 1B+, 2B, or 3B
- HDMI display
- UART serial cable (optional, for debugging)
- PWM-capable audio output (optional, GPIO 40)
- SD card for boot files

## Building

### Prerequisites

Install the ARM cross-compiler toolchain:

```bash
# On Ubuntu/Debian
sudo apt-get install gcc-arm-none-eabi

# On macOS with Homebrew
brew install --cask gcc-arm-embedded
```

### Build Commands

```bash
# Build for Raspberry Pi 0/1 (BCM2835 - default)
make

# Build for Raspberry Pi 2 (BCM2836)
make bcm2836

# Build for Raspberry Pi 3 (BCM2837)
make bcm2837

# Clean build artifacts
make clean

# Show help
make help
```

The build produces `build/kernel.img` which should be copied to your SD card.

## Installation

1. Format an SD card as FAT32
2. Download Raspberry Pi firmware files:
   - `bootcode.bin`
   - `start.elf`
   - `fixup.dat`
3. Copy `build/kernel.img` to the SD card
4. (Optional) Add `config.txt` with desired settings
5. Insert SD card into Raspberry Pi and power on

### Optional: config.txt

```ini
# Force HDMI output
hdmi_force_hotplug=1
hdmi_drive=2

# Set resolution
hdmi_group=2
hdmi_mode=4

# Enable UART
enable_uart=1
```

## Features in Detail

### PWM Boot Beep

The bootloader plays a distinctive three-tone beep sequence on boot:
- High tone (800 Hz, 150ms)
- Low tone (400 Hz, 150ms)  
- Mid tone (600 Hz, 200ms)

Requires a speaker connected to GPIO 40 (PWM pin).

### Scanline Effect

Authentic CRT monitor scanline effect darkens alternating horizontal lines by 25%, giving the display a vintage terminal appearance.

### Memory Test

Displays randomized memory test patterns showing:
- Memory addresses (0x00100000+)
- Test patterns (random hex values)
- Status indicators

### Bad Sector Warnings

15% chance on each boot to display a warning about a simulated bad sector, adding to the authentic Fallout terminal experience.

### Diagnostic Mode

Press 'D' on the UART console during boot to enter diagnostic mode, which displays:
- Hardware component status
- System information
- Debug data

Press any key to exit diagnostic mode and continue boot.

### Chain-Loading

The bootloader can load a second-stage bootloader or kernel from the SD card. It:
1. Initializes the SD card interface
2. Reads the boot sector (block 0)
3. Validates boot signatures
4. Loads the next stage into memory
5. Transfers control to the loaded code

## Architecture

### Directory Structure

```
RETROS-BIOS/
├── include/           # Header files
│   ├── hardware.h    # Hardware register definitions
│   ├── uart.h        # UART driver
│   ├── framebuffer.h # Display driver
│   ├── font.h        # 8x16 font
│   ├── pwm_audio.h   # PWM audio driver
│   └── sdcard.h      # SD card driver
├── src/              # Source files
│   ├── boot.S        # Boot assembly code
│   ├── main.c        # Main bootloader
│   ├── hardware.c    # Hardware utilities
│   ├── uart.c        # UART implementation
│   ├── framebuffer.c # Framebuffer implementation
│   ├── font.c        # Font data
│   ├── pwm_audio.c   # PWM audio implementation
│   └── sdcard.c      # SD card implementation
├── linker.ld         # Linker script
├── Makefile          # Build system
└── README.md         # This file
```

### Boot Process

1. **Boot.S**: ARM assembly entry point
   - Checks CPU ID (only CPU 0 continues)
   - Sets up stack pointer
   - Clears BSS section
   - Jumps to kernel_main()

2. **Main.c**: Main bootloader logic
   - Initializes UART for debugging
   - Sets up framebuffer (640x480)
   - Plays boot beep via PWM
   - Displays animated boot messages
   - Shows memory test patterns
   - Randomly displays bad sector warnings
   - Applies scanline effect
   - Checks for diagnostic mode
   - Chain-loads next stage from SD card

### Hardware Abstraction

The bootloader uses compile-time configuration to support different BCM chips:

```c
#if defined(BCM2836)
    #define PERIPHERAL_BASE 0x3F000000  // RPi2
#elif defined(BCM2837)
    #define PERIPHERAL_BASE 0x3F000000  // RPi3
#else
    #define PERIPHERAL_BASE 0x20000000  // RPi0/1
#endif
```

## Debugging

### UART Console

Connect a USB-to-TTL serial adapter:
- TX (adapter) → RX (GPIO 15)
- RX (adapter) → TX (GPIO 14)
- GND → GND

Use a serial terminal at 115200 baud:

```bash
# Linux/macOS
screen /dev/ttyUSB0 115200

# Or use minicom
minicom -D /dev/ttyUSB0 -b 115200
```

All boot messages are sent to both UART and framebuffer.

### Building with Debug Info

The build automatically generates a disassembly listing in `build/kernel.list` for debugging.

## Customization

### Changing Colors

Edit color definitions in `src/main.c`:

```c
#define COLOR_BLACK     0x00000000
#define COLOR_GREEN     0x0000FF00
#define COLOR_DKGREEN   0x00008000
#define COLOR_AMBER     0x00FFA500
```

### Modifying Boot Messages

Edit the messages in `kernel_main()` in `src/main.c`.

### Adjusting Boot Beep

Modify the frequencies and durations in `pwm_boot_beep()` in `src/pwm_audio.c`:

```c
pwm_play_beep(800, 150);  // frequency_hz, duration_ms
```

### Bad Sector Warning Probability

Change the probability in `bad_sector_warning()` in `src/main.c`:

```c
if ((random() % 100) < 15) {  // 15% chance
```

## Technical Details

### Memory Map

- **0x00000000**: Exception vectors (GPU-managed on RPi)
- **0x00008000**: Kernel load address (RETROS-BIOS entry point)
- **Stack**: Grows downward from kernel_end + 32KB
- **Peripherals**: BCM2835=0x20000000, BCM2836/7=0x3F000000

### Boot Sequence Timing

1. GPU bootcode.bin loads (0-2s)
2. start.elf loads kernel.img to 0x8000 (0-1s)
3. RETROS-BIOS boot.S executes (instantaneous)
4. Hardware initialization (100-500ms)
5. Boot beep (350ms)
6. Animated messages (2-4s)
7. Memory test display (800ms)
8. Chain-load preparation (1-2s)

Total boot time: ~5-9 seconds

## Known Limitations

- SD card driver is simplified (basic read support only)
- No USB support (would require complex USB/DWCOTG driver)
- No interrupt handling (polled I/O only)
- Font limited to 8x16 VGA character set (ASCII 0x20-0x7E)
- PWM audio is basic square wave (no PCM/DMA)
- Single-core only (other cores are halted)

## Future Enhancements

- [ ] Full EMMC SD card driver with write support
- [ ] FAT32 filesystem for loading kernels by name
- [ ] USB keyboard input
- [ ] More elaborate visual effects (flicker, noise)
- [ ] Sound effects beyond boot beep
- [ ] Network boot via Ethernet
- [ ] Multi-core support

## Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

## License

MIT License - See LICENSE file for details.

## Acknowledgments

- Inspired by the Fallout game series terminal aesthetics
- Based on Raspberry Pi bare-metal programming tutorials
- Font data derived from standard VGA BIOS fonts
- Thanks to the Raspberry Pi community

## References

- [Raspberry Pi Hardware Documentation](https://www.raspberrypi.org/documentation/hardware/)
- [BCM2835 ARM Peripherals Guide](https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf)
- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/)
- [OSDev Wiki - Raspberry Pi Bare Bones](https://wiki.osdev.org/Raspberry_Pi_Bare_Bones)

---

**RobCo Industries™** - *"Engineering a Better Future, Today!"*
