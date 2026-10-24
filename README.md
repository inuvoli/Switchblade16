# SwitchBlade 16 Firmware Reverse Engineering

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Status](https://img.shields.io/badge/status-reverse--engineering-orange)]()

A complete technical documentation of the **SoundSculpture SwitchBlade 16** audio routing switcher firmware (version 1.51), derived through reverse engineering of the original binary.

---

## Table of Contents

- [Overview](#overview)
- [Hardware Specifications](#hardware-specifications)
- [Memory Map](#memory-map)
- [MIDI Implementation](#midi-implementation)
- [Preset Structure](#preset-structure)
- [Gain Engine](#gain-engine)
- [Project Structure](#project-structure)
- [Building the Firmware](#building-the-firmware)
- [Disclaimer](#disclaimer)

---

## Overview

The **SwitchBlade 16** (SB16) is a MIDI-controlled audio switcher manufactured by SoundSculpture. It provides 32 independent "links", each routing one of 16 audio inputs to one of 16 outputs through crosspoint matrices, with per-link gain control via DAC channels.

This repository documents the firmware extracted from a **Firmware_SB16_1.51.bin** image (32 KB), analyzed using Ghidra for the 8051 architecture. The device is built around an **80C31 microcontroller** running at 12 MHz with no internal ROM—all code and data reside on external memory.

### Key Features

| Feature | Specification |
|---------|---------------|
| **Links** | 32 independent audio paths |
| **I/O** | 16 inputs × 16 outputs |
| **Presets** | 75 user presets in EEPROM |
| **Control** | MIDI Program Change, front panel, foot pedals |
| **Morphing** | 2 continuous controllers (CTR0/CTR1) or timer sweep |
| **Display** | 20×2 HD44780-compatible LCD |
| **CPU** | 80C31 @ 12 MHz (external memory) |

---

## Hardware Specifications

| Component | Part Number | Function |
|-----------|-------------|----------|
| CPU | 80C31 | 8051-compatible microcontroller |
| ROM | 27C256 | 32 KB EPROM (Firmware) |
| EEPROM | AT28PC64 | 8KB EEPROM (Presets) |
| RAM | UM6116-2 | 2KB RAM (External RAM, only 512B ssable)|
| DAC | MP7529B | 8-bit multiplying DAC (×16) |
| Matrix | CD74HC22106 | 8×8 analog crosspoint (×16) |
| LCD | HD44780-compatible | 20×2 character display |

### Signal Path

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│ 8 × CD74HC  │     │ 16 × MP7529B │     │ 8 × CD74HC  │
│ 22106       │ ──> │ DAC Chips    │ ──> │ 22106       │
│ Input Mat.  │     │ (Gain/Layer) │     │ Output Mat. │
└─────────────┘     └──────────────┘     └─────────────┘
     (16→16)           (per-link)            (16→16)
```

### Memory Configuration

| Component | Size | Address Range | Notes |
|-----------|------|---------------|-------|
| **Code ROM** | 32 KB | 0x0000–0x7FFF | 27C256, original FW code ends at 0x3948 |
| **EEPROM** | 8 KB | 0x8000–0x9FFF | Parallel, DATA# polling |
| **RAM** | 512 B | 0x7E00–0x7FFF | External SRAM |
| **Internal RAM** | 128 B | 0x00–0x7F | 80C31 internal |

---

## Memory Map

### External Data Space

```
0x0000 ┌─────────────────┐
       │   NOT USED      │ 
0x1E00 ├─────────────────┤
       │   I/O Registers │ (LCD, LED, DAC, Matrix, ..)
0x3E00 ├─────────────────┤
       │   NOT USED      │ 
0x7E00 ├─────────────────┤
       │   External RAM  │ (512 bytes)
0x7FFF ├─────────────────┤
       │   EEPROM        │ (8 KB, presets + config)
0x9FFF ├─────────────────┤
       │   NOT USED      │ 
0xFFFF └─────────────────┘
```

### I/O Address Map

| Address | Device | Function |
|---------|--------|----------|
| 0x1E00–0x1E02 | LCD | Command / Data / Busy |
| 0x1E08–0x1E0F | Relay Latches | 8 relay outputs |
| 0x1E10–0x1E12 | LEDs | PRG / PLAY / CNT indicators |
| 0x1E18 | SBSR | Status Register. Control Output buffer mute, Vref value, input pad control |
| 0x2E00 / 0x3600 | DACs | 16 x MP7529B chips (links 16–31 / 0–15) |
| 0x3A00–0x3BFF | Output Matrix | 8 × CD74HC22106 |
| 0x3C00–0x3DFF | Input Matrix | 8 × CD74HC22106 |

### EEPROM Layout

| Address Range | Content |
|---------------|---------|
| 0x8000–0x9CEF | 75 presets × 99 bytes |
| 0x9D0F | Relay configuration (pulse/steady) |
| 0x9D10–0x9E0F | 32 I/O names × 8 chars |
| 0x9E10–0x9E1F | Per-output MIDI channels |
| 0x9E20 | SwitchBlade MIDI channel |
| 0x9E21 | Preset switch time (crossfade) |
| 0x9E25/0x9E26 | CTR0 / CTR1 CC numbers |
| 0x9E30+ | Banks Configuration (20 banks with max 10 Presets each) |

---

## MIDI Implementation

### Supported Messages

| Message | Status | Channel Filter | Action |
|---------|--------|----------------|--------|
| **Program Change** | `0xC0` | Yes (`SMC`) | Recall preset `PC + 1` |
| **Channel Pressure** | `0xD0` | Yes | Set CTR0 position |
| **Pitch Bend** | `0xE0` | Yes | Set CTR0 position (MSB only) |
| **Control Change** | `0xB0` | Yes | CTR0/CTR1 if CC matches |
| **Note On** | `0x90` | Yes | Remote mode request |
| **System Exclusive** | `0xF0` | No | Legacy bulk commands |
| **Real-Time** | `0xF8+` | No | Ignored |

### Example: Recall Preset 5 via MIDI

```python
# MIDI Program Change on channel 1 (0xC0), preset number 4 (0-based)
midi_message = [0xC0, 0x04]
```

### Example: Set CTR0 to 50% via CC

```python
# CC #7 (default CTR0), value 64 = 50%
midi_message = [0xB0, 0x07, 0x40]
```

### Running Status

The firmware supports MIDI running status—after a status byte, subsequent messages of the same type can omit the status byte.

---

## Preset Structure

Each preset occupies **99 bytes** in EEPROM:

```
Address = 0x8000 + (preset_number - 1) × 99
```

| Offset | Size | Content |
|--------|------|---------|
| 0x00–0x3F | 64 | 32 links × 2 bytes (attenuation + routing) |
| 0x40 | 1 | Relay bits |
| 0x41–0x50 | 16 | Program Change numbers per output (0xFF = unused) |
| 0x51–0x5C | 12 | Preset name (ASCII) |
| 0x5D | 1 | Sweep delay (SD) |
| 0x5E | 1 | Sweep mode (0=off, 1=smooth, 2=multi-pan) |
| 0x5F–0x62 | 4 | 32-bit mask: bit set = link follows CTR1, clear = CTR0 |

### Link Structure (2 bytes per link)

```
Byte 0: att = [end_level:4][start_level:4]
Byte 1: route = [output:4][input:4]
```

**Example:** Link routing input 3 to output 7, with gain morphing from -12 dB to 0 dB:

```python
att = (0xD << 4) | 0x9    # start=-12dB (code 9), end=0dB (code D)
route = (7 << 4) | 3      # input=3, output=7
link_bytes = [att, route]  # [0xD9, 0x73]
```

### Gain Codes

| Code | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | A | B | C | D | E | F |
|------|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **dB** | off | -42 | -36 | -30 | -27 | -24 | -21 | -18 | -15 | -12 | -9 | -6 | -3 | 0 | +3 | +6 |
| **DAC** | 0x00 | 0x01 | 0x02 | 0x04 | 0x06 | 0x08 | 0x0B | 0x10 | 0x17 | 0x20 | 0x2D | 0x40 | 0x5A | 0x80 | 0xB4 | 0xFF |

---

## Gain Engine

### Morphing Algorithm

The firmware interpolates between start and end gain values based on controller position (0–255):

```c
uint8_t dac_value(uint8_t att, uint8_t pos) {
    uint8_t lo = gain_table[att & 0x0F];      // start level
    uint8_t hi = gain_table[att >> 4];        // end level
    
    if (lo == hi) {
        return lo;
    } else if (lo < hi) {
        // Ramp up: lo + ((hi - lo) * pos) >> 8
        return lo + ((hi - lo) * pos >> 8);
    } else {
        // Ramp down: hi + ((lo - hi) * ~pos) >> 8
        return hi + ((lo - hi) * ~pos >> 8);
    }
}
```

### Example: Calculate DAC Value

```python
gain_table = [0x00, 0x01, 0x02, 0x04, 0x06, 0x08, 0x0B, 0x10,
              0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF]

def dac_value(att, pos):
    lo = gain_table[att & 0x0F]
    hi = gain_table[att >> 4]
    
    if lo == hi:
        return lo
    elif lo < hi:
        return lo + ((hi - lo) * pos >> 8)
    else:
        return hi + ((lo - hi) * ~pos >> 8)

# Example: att=0xD9 (start -12dB, end 0dB), pos=128 (50%)
result = dac_value(0xD9, 128)  # Returns ~0x50 (midpoint)
```

### Sweep Modes

| Mode | Name | Description |
|------|------|-------------|
| 0 | Off | Controllers set static positions |
| 1 | Smooth | Timer-driven triangle wave (CTR1 sweeps 0↔255) |
| 2 | Multi-pan | Complex multi-link morphing pattern |

---

## Project Structure

```
Switchblade16/
├── README.md                 # This file
├── build.cmd                 # Windows build script (SDCC)
├── build.sh                  # Linux build script (SDCC)
├── src/
│   └── sb16_3.00.c           # Reconstructed C source
├── bin/                      # Compiled output (.bin, .hex)
├── build/                    # Intermediate build files

```

---

## Building the Firmware

### Prerequisites

- **SDCC** (Small Device C Compiler) ≥ 4.0
- **packihx** and **makebin** (included with SDCC)

### Windows

```cmd
build.cmd
```

### Linux

```bash
chmod +x build.sh
./build.sh
```

### Build Output

| File | Location | Description |
|------|----------|-------------|
| `sb16_3.00.ihx` | `build/` | Intel HEX (raw compiler output) |
| `sb16_3.00.hex` | `build/` | Intel HEX (packed, programmer-ready) |
| `sb16_3.00.bin` | `bin/` | Binary image (32 KB, padded) |

### Compiler Flags

```bash
sdcc -mmcs51 --model-small      --iram-size 128      --xram-loc 0x7E00      --xram-size 0x200      --code-size 0x8000      -o build/ ./src/sb16_3.00.c
```

| Flag | Value | Purpose |
|------|-------|---------|
| `--iram-size` | 128 | Internal RAM (standard 8051) |
| `--xram-loc` | 0x7E00 | External RAM start address |
| `--xram-size` | 0x200 | External RAM size (512 bytes) |
| `--code-size` | 0x8000 | Code memory size (32 KB) |

---

## Disclaimer

**This project is for educational and preservation purposes only.**

- The original firmware is copyrighted by SoundSculpture.
- This documentation is derived from reverse engineering and may contain inaccuracies.
- No warranty is provided—use at your own risk.
- Do not use this information for commercial purposes without authorization.

---

## Contributing

Contributions are welcome! If you have:

- Corrections to the memory map or register definitions
- Additional MIDI message documentation
- Improvements to the reconstructed C source
- Hardware schematics or photos

Please open an issue or submit a pull request.

---

## License

This documentation is released under the [MIT License](LICENSE).

---

**Repository:** [github.com/inuvoli/Switchblade16](https://github.com/inuvoli/Switchblade16)  
**Last Updated:** 2024

