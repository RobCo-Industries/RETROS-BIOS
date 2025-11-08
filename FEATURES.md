# RETROS-BIOS Feature Guide

## Fallout Terminal Aesthetics

This guide details all the Fallout-inspired features implemented in RETROS-BIOS.

## Boot Sequence

### 1. Boot Beep (PWM Audio)

**Description**: Three-tone audio sequence plays on boot if a speaker is connected to GPIO 40.

**Sequence**:
- High tone: 800 Hz for 150ms
- 50ms pause
- Low tone: 400 Hz for 150ms  
- 50ms pause
- Mid tone: 600 Hz for 200ms

**Hardware Required**: Speaker or piezo buzzer connected between GPIO 40 and GND.

**Code Location**: `src/pwm_audio.c::pwm_boot_beep()`

**Customization**:
```c
// In src/pwm_audio.c
pwm_play_beep(frequency_hz, duration_ms);
```

### 2. Animated Boot Messages

**Description**: Boot messages appear with a typewriter effect, character by character.

**Speed**: 10ms delay per character (adjustable)

**Features**:
- Messages appear on both HDMI and UART
- Green text on black background
- Authentic terminal feel

**Code Location**: `src/main.c::boot_message_animated()`

**Customization**:
```c
// Change typing speed in src/main.c
delay_ms(10);  // Change this value
```

### 3. Memory Test Pattern

**Description**: Displays randomized memory addresses and test patterns.

**Display**:
```
MEMORY TEST:
  0x00100000: 3A7F2C OK
  0x00200000: 8B4E91 OK
  0x00300000: C2D5A3 OK
  ...
```

**Features**:
- 8 memory ranges tested
- Random test patterns for each boot
- 100ms delay between each line

**Code Location**: `src/main.c::memory_test_pattern()`

### 4. Bad Sector Warning

**Description**: Randomly displays a warning about a bad sector (15% probability per boot).

**Example Output**:
```
WARNING: Bad sector detected: 7834
```

**Probability**: 15% (configurable)

**Code Location**: `src/main.c::bad_sector_warning()`

**Customization**:
```c
// In src/main.c::bad_sector_warning()
if ((random() % 100) < 15) {  // Change 15 to adjust probability
```

### 5. Scanline Effect

**Description**: CRT-style scanline effect darkens alternating horizontal lines.

**Implementation**: Every other horizontal line is darkened by 25%

**Visual Effect**: 
- Creates authentic CRT monitor appearance
- Applied after all boot messages
- Works on any resolution

**Code Location**: `src/framebuffer.c::fb_apply_scanlines()`

**Customization**:
```c
// In src/framebuffer.c::fb_apply_scanlines()
// Adjust darkening factor (currently 3/4)
uint32_t r = ((color >> 16) & 0xFF) * 3 / 4;  // Change 3/4 ratio
```

## Color Scheme

### Standard Colors

Defined in `src/main.c`:

```c
#define COLOR_BLACK     0x00000000  // Background
#define COLOR_GREEN     0x0000FF00  // Primary text
#define COLOR_DKGREEN   0x00008000  // Secondary text
#define COLOR_AMBER     0x00FFA500  // Warnings
#define COLOR_RED       0x00FF0000  // Errors
```

### Customization

Change color values in hex RGBA format:
- Format: 0xRRGGBBAA (Alpha is 00 for now)
- Example: `0x0000FF00` = Green

## Hidden Features

### Diagnostic Mode

**Activation**: Press 'D' on UART console during the boot message phase.

**Display**:
```
=== DIAGNOSTIC MODE ===
Hardware Status:
  UART0: OK
  Framebuffer: OK
  Timer: OK
  PWM: OK
  GPIO: OK

Press any key to exit...
```

**Duration**: Until any key is pressed

**Code Location**: `src/main.c::diagnostic_mode()`

**Exit**: Press any key to return to normal boot

### Key Combinations

Currently implemented:
- **'D'**: Enter diagnostic mode

**Future Key Combos** (ready for expansion):
- Could add 'S' for system info
- Could add 'T' for extended tests
- Could add 'H' for help screen

**Extension Point**: `src/main.c::check_diagnostic_mode()`

## Chain-Loading

### Next Stage Boot

**Purpose**: Load a second-stage bootloader or kernel from SD card.

**Process**:
1. Initialize SD card interface
2. Read boot sector (block 0)
3. Verify signature (placeholder)
4. Load kernel to memory (placeholder)
5. Jump to entry point (placeholder)

**Current Status**: Framework in place, needs full SD card driver

**Code Location**: `src/main.c::chain_load_next_stage()`

### SD Card Support

**Current Implementation**: Basic framework

**Features Needed for Full Support**:
- Complete EMMC controller initialization
- CMD protocol implementation
- Block read/write operations
- FAT32 filesystem

**Code Location**: `src/sdcard.c`

## Terminal Features

### Font

**Type**: 8x16 VGA-style bitmap font

**Character Set**: ASCII 0x20-0x7E (printable characters)

**Implementation**: Baked into binary (`src/font.c`)

**Rendering**: Software rendered, pixel-by-pixel

### Text Display

**Functions**:
- `fb_draw_char()`: Single character
- `fb_draw_string()`: Full string
- `fb_draw_pixel()`: Individual pixel

**Colors**: Foreground and background per character

**Location**: `src/framebuffer.c`

## UART Debugging

### Features

**Output Functions**:
- `uart_puts()`: String output
- `uart_putc()`: Character output
- `uart_printf()`: Formatted output (basic)

**Input Functions**:
- `uart_getc()`: Blocking character read
- `uart_data_available()`: Non-blocking check

**Baud Rate**: 115200 (configured in `uart_init()`)

**Format**: 8-N-1 (8 data bits, no parity, 1 stop bit)

**Location**: `src/uart.c`

### Printf Support

**Supported Format Specifiers**:
- `%d`: Decimal integer
- `%x`: Hexadecimal (with 0x prefix)
- `%s`: String
- `%c`: Character
- `%%`: Literal %

**Example**:
```c
uart_printf("Boot sector: %x, Status: %s\n", addr, "OK");
```

## Hardware Support

### GPIO Pins Used

- **GPIO 14**: UART TX
- **GPIO 15**: UART RX
- **GPIO 40**: PWM Audio Output

### Peripherals

**UART0**:
- Base: PERIPHERAL_BASE + 0x201000
- Baud: 115200
- Mode: 8-N-1

**Framebuffer**:
- Interface: Mailbox property interface
- Resolution: 640x480 (default)
- Depth: 32-bit RGBA

**PWM**:
- Base: PERIPHERAL_BASE + 0x20C000
- Mode: PWM (not serializer)
- Clock: 9.6 MHz

**System Timer**:
- Base: PERIPHERAL_BASE + 0x3000
- Used for: Delays and timing

## Performance

### Boot Time

Typical boot sequence:
- GPU boot: 1-2 seconds
- BIOS initialization: 0.5 seconds
- Boot beep: 0.35 seconds
- Boot messages: 2-4 seconds (animated)
- Memory test: 0.8 seconds
- Chain-load prep: 1-2 seconds
- **Total**: ~5-9 seconds

### Memory Usage

**Binary Size**: ~12 KB
**Stack**: 32 KB (configured in linker.ld)
**Framebuffer**: Allocated by GPU (depends on resolution)

### Optimization

Current optimization: `-O2`

Options:
- `-O0`: No optimization (for debugging)
- `-O1`: Basic optimization
- `-O2`: Recommended (current)
- `-O3`: Aggressive optimization
- `-Os`: Size optimization

## Customization Examples

### Change Boot Message

Edit `src/main.c::kernel_main()`:

```c
boot_message_animated(16, y, "Your custom message here", COLOR_GREEN);
```

### Add New Boot Beep Pattern

Edit `src/pwm_audio.c::pwm_boot_beep()`:

```c
void pwm_boot_beep(void) {
    pwm_play_beep(1000, 100);  // Your frequency and duration
    delay_ms(50);
    pwm_play_beep(500, 100);
    // Add more tones...
}
```

### Change Scanline Intensity

Edit `src/framebuffer.c::fb_apply_scanlines()`:

```c
// Make scanlines darker (2/4 instead of 3/4)
uint32_t r = ((color >> 16) & 0xFF) * 2 / 4;
uint32_t g = ((color >> 8) & 0xFF) * 2 / 4;
uint32_t b = (color & 0xFF) * 2 / 4;
```

### Add More Colors

In `src/main.c`:

```c
#define COLOR_CYAN      0x00FFFF00
#define COLOR_YELLOW    0x00FFFF00
#define COLOR_MAGENTA   0x00FF00FF
```

## Testing

### Unit Tests

Currently: Manual testing required

### Integration Tests

Test each feature:
1. ✓ UART output (check with serial terminal)
2. ✓ Framebuffer (verify HDMI output)
3. ✓ Boot beep (listen for audio)
4. ✓ Scanlines (visual check on monitor)
5. ✓ Memory test (verify randomness across boots)
6. ✓ Bad sector (boot multiple times to see it appear)
7. ✓ Diagnostic mode (press 'D' during boot)

### Hardware Tests

Test on actual hardware:
- Raspberry Pi Zero
- Raspberry Pi 1
- Raspberry Pi 2
- Raspberry Pi 3

## Known Limitations

1. **SD Card**: Basic framework only, needs full driver
2. **USB**: Not supported
3. **Interrupts**: Not implemented (polled I/O only)
4. **Multi-core**: Only CPU 0 runs (others halted)
5. **Network**: No Ethernet support
6. **Audio**: Square wave only (no PCM)

## Future Enhancements

- [ ] Add visual flicker effect
- [ ] Add screen noise/static
- [ ] Add more sound effects
- [ ] Implement USB keyboard input
- [ ] Add boot menu system
- [ ] Network boot capability
- [ ] More diagnostic screens
- [ ] Save boot log to SD card
- [ ] Add boot animation

---

**Note**: This is a bare-metal bootloader. There is no operating system running beneath it. Everything runs directly on the hardware.
