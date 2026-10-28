# EEPROM Memory Layout

This document provides a detailed description of the EEPROM memory organization for the **SwitchBlade 16** audio routing switcher. The EEPROM stores all user presets and device global configuration settings.

---

## Overview

| Parameter | Specification |
|-----------|---------------|
| **EEPROM Type** | AT28PC64 (Parallel EEPROM) |
| **Total Size** | 8 KB (8192 bytes) |
| **Address Range** | 0x8000 – 0x9FFF |
| **Base Address** | `EE_BASE = 0x8000` |

---

## Memory Map Summary

| Address Range | Offset from Base | Content | Size |
|---------------|------------------|---------|------|
| 0x8000 – 0x9CEF | 0x0000 – 0x1CEF | 75 User Presets | 7425 bytes |
| 0x9D0F | 0x1D0F | Relay Configuration (RC) | 1 byte |
| 0x9D10 – 0x9E0F | 0x1D10 – 0x1E0F | I/O Names (32 × 8 chars) | 256 bytes |
| 0x9E10 – 0x9E1F | 0x1E10 – 0x1E1F | Output MIDI Channels (OMC) | 16 bytes |
| 0x9E20 | 0x1E20 | SwitchBlade MIDI Channel (SMC) | 1 byte |
| 0x9E21 | 0x1E21 | Preset Switch Time (PST) | 1 byte |
| 0x9E22 | 0x1E22 | Input 1 Pad (IP) | 1 byte |
| 0x9E23 | 0x1E23 | Program Manager Status (PMS) | 1 byte |
| 0x9E24 | 0x1E24 | Last Bank Index (LB) | 1 byte |
| 0x9E25 | 0x1E25 | CTR0 CC Number | 1 byte |
| 0x9E26 | 0x1E26 | CTR1 CC Number | 1 byte |
| 0x9E27 – 0x9E2F | 0x1E27 – 0x1E2F | Reserved | 9 bytes |
| 0x9E30 – 0x9F5F | 0x1E30 – 0x1F5F | Banks Configuration (20 × 10) | 200 bytes |
| 0x9F60 – 0x9FDF | 0x1F60 – 0x1FDF | Reserved / Backup Area | 128 bytes |
| 0x9FE0 – 0x9FFD | 0x1FE0 – 0x1FFD | SysEx Backup Configuration | 30 bytes |
| 0x9FFE – 0x9FFF | 0x1FFE – 0x1FFF | Backup End Marker | 2 bytes |

---

## Preset Memory Region (0x8000 – 0x9CEF)

### Preset Allocation

Each preset occupies **99 bytes** in EEPROM. The device supports **75 user presets**.

```
Preset Address = 0x8000 + (preset_number - 1) × 99
```

| Preset # | Start Address | End Address |
|----------|---------------|-------------|
| 1 | 0x8000 | 0x8062 |
| 2 | 0x8063 | 0x80C5 |
| 3 | 0x80C6 | 0x8128 |
| … | … | … |
| 75 | 0x9CAF | 0x9CEF |

### Preset Structure (99 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 – 0x3F | 64 bytes | Link Data | 32 links × 2 bytes each |
| 0x40 | 1 byte | RLY | Relay states (8 bits) |
| 0x41 – 0x50 | 16 bytes | PC1 – PC16 | Program Change numbers per output |
| 0x51 – 0x5C | 12 bytes | Preset Name | ASCII characters (null-padded) |
| 0x5D | 1 byte | SD | Sweep Delay |
| 0x5E | 1 byte | SM | Sweep Mode |
| 0x5F – 0x62 | 4 bytes | SK1 – SK4 | Sweep Mask (32-bit) |

---

### Link Data Structure (Bytes 0x00 – 0x3F)

Each of the 32 links is represented by **2 consecutive bytes**:

```
Byte N×2   (attenuation):  [  End Level (4 bits)  |  Start Level (4 bits)  ]
Byte N×2+1 (routing):      [    Output (4 bits)   |     Input (4 bits)     ]
```

#### Attenuation Byte

| Bits | Content |
|------|---------|
| 7–4 | End level (gain at controller maximum) |
| 3–0 | Start level (gain at controller minimum) |

**Gain Codes:**

| Code | Hex | dB | DAC Value |
|------|-----|----|-----------|
| 0 | 0x0 | OFF | 0x00 |
| 1 | 0x1 | -42 dB | 0x01 |
| 2 | 0x2 | -36 dB | 0x02 |
| 3 | 0x3 | -30 dB | 0x04 |
| 4 | 0x4 | -27 dB | 0x06 |
| 5 | 0x5 | -24 dB | 0x08 |
| 6 | 0x6 | -21 dB | 0x0B |
| 7 | 0x7 | -18 dB | 0x10 |
| 8 | 0x8 | -15 dB | 0x17 |
| 9 | 0x9 | -12 dB | 0x20 |
| 10 | 0xA | -9 dB | 0x2D |
| 11 | 0xB | -6 dB | 0x40 |
| 12 | 0xC | -3 dB | 0x5A |
| 13 | 0xD | 0 dB | 0x80 |
| 14 | 0xE | +3 dB | 0xB4 |
| 15 | 0xF | +6 dB | 0xFF |

#### Routing Byte

| Bits | Content |
|------|---------|
| 7–4 | Output number (0–15) |
| 3–0 | Input number (0–15) |

**Example:** Link routing input 3 to output 7:
```
route = (7 << 4) | 3 = 0x73
```

---

### Relay Configuration (Byte 0x40)

| Bit | Relay | State |
|-----|-------|-------|
| 0 | RLY1 | 0 = Open, 1 = Closed |
| 1 | RLY2 | 0 = Open, 1 = Closed |
| 2 | RLY3 | 0 = Open, 1 = Closed |
| 3 | RLY4 | 0 = Open, 1 = Closed |
| 4 | RLY5 | 0 = Open, 1 = Closed |
| 5 | RLY6 | 0 = Open, 1 = Closed |
| 6 | RLY7 | 0 = Open, 1 = Closed |
| 7 | RLY8 | 0 = Open, 1 = Closed |

---

### Program Change Assignments (Bytes 0x41 – 0x50)

| Offset | Field | Description |
|--------|-------|-------------|
| 0x41 | PC1 | Program Change for Output 1 |
| 0x42 | PC2 | Program Change for Output 2 |
| … | … | … |
| 0x50 | PC16 | Program Change for Output 16 |

**Values:**
- `0x00 – 0x7F` (0–127): Program Change number
- `0xFF` (255): Unused / No assignment

---

### Preset Name (Bytes 0x51 – 0x5C)

| Offset | Size | Content |
|--------|------|---------|
| 0x51 – 0x5C | 12 bytes | ASCII characters |

- Characters are stored as standard ASCII
- Unused positions are padded with `0x00` or spaces
- Valid characters: A–Z, 0–9, space

---

### Sweep Configuration (Bytes 0x5D – 0x62)

#### Sweep Delay (Byte 0x5D, SD)

| Value | Delay |
|-------|-------|
| 0x01 | 250 ms |
| 0x02 | 300 ms |
| … | … |
| 0xFF | 12,950 ms |

**Formula:** `delay_ms = 250 + (value - 1) × 50`

#### Sweep Mode (Byte 0x5E, SM)

| Value | Mode | Description |
|-------|------|-------------|
| 0 | Off | Controllers set static positions |
| 1 | Smooth | Timer-driven triangle wave (CTR1 sweeps 0↔255) |
| 2 | Multi-pan | Complex multi-link morphing pattern |

#### Sweep Mask (Bytes 0x5F – 0x62, SK1 – SK4)

32-bit mask controlling which links follow CTR1 vs CTR0:

- **Bit set (1):** Link follows **CTR1**
- **Bit clear (0):** Link follows **CTR0**

| Byte | Bits | Links |
|------|------|-------|
| 0x5F (SK1) | 0–7 | Links 0–7 |
| 0x60 (SK2) | 8–15 | Links 8–15 |
| 0x61 (SK3) | 16–23 | Links 16–23 |
| 0x62 (SK4) | 24–31 | Links 24–31 |

---

## Global Configuration Region (0x9D0F – 0x9FFF)

### Relay Configuration Mask (0x9D0F, RC)

| Bit | Relay | Mode |
|-----|-------|------|
| 0 | RLY1 | 0 = Steady, 1 = Pulse |
| 1 | RLY2 | 0 = Steady, 1 = Pulse |
| … | … | … |
| 7 | RLY8 | 0 = Steady, 1 = Pulse |

---

### I/O Names (0x9D10 – 0x9E0F)

32 input/output names, 8 characters each:

| Address Range | Content |
|---------------|---------|
| 0x9D10 – 0x9D8F | Input names (IN1 – IN16) |
| 0x9D90 – 0x9E0F | Output names (OUT1 – OUT16) |

**Structure per name:**
- 8 bytes, ASCII characters
- Null-padded if shorter than 8 characters

---

### Output MIDI Channels (0x9E10 – 0x9E1F, OMC1 – OMC16)

| Offset | Field | Description |
|--------|-------|-------------|
| 0x9E10 | OMC1 | MIDI Channel for Output 1 |
| 0x9E11 | OMC2 | MIDI Channel for Output 2 |
| … | … | … |
| 0x9E1F | OMC16 | MIDI Channel for Output 16 |

**Values:** `0x00 – 0x0F` (MIDI channels 1–16)

---

### Device Settings (0x9E20 – 0x9E2F)

| Address | Field | Name | Description |
|---------|-------|------|-------------|
| 0x9E20 | SMC | SwitchBlade MIDI Channel | Receive channel (0–15) |
| 0x9E21 | PST | Preset Switch Time | Crossfade time (0–1000 ms in 100 ms steps) |
| 0x9E22 | IP | Input 1 Pad | 0x00 = OFF, 0x01 = ON (-6 dB) |
| 0x9E23 | PMS | Program Manager Status | 0x00 = OFF, 0x01 = ON |
| 0x9E24 | LB | Last Bank Index | Index of last populated bank (0–19) |
| 0x9E25 | CTR0 | Controller 0 CC Number | MIDI CC number for CTR0 |
| 0x9E26 | CTR1 | Controller 1 CC Number | MIDI CC number for CTR1 |
| 0x9E27 – 0x9E2F | — | Reserved | Future use (fill with 0xFF) |

#### Preset Switch Time (PST) Values

| Value | Time |
|-------|------|
| 0x0 | 0 ms |
| 0x1 | 100 ms |
| 0x2 | 200 ms |
| … | … |
| 0xA | 1000 ms |

---

### Banks Configuration (0x9E30 – 0x9F5F)

20 banks, each containing up to 10 preset numbers:

| Bank | Address Range | Size |
|------|---------------|------|
| Bank 1 | 0x9E30 – 0x9E39 | 10 bytes |
| Bank 2 | 0x9E40 – 0x9E49 | 10 bytes |
| … | … | … |
| Bank 20 | 0x9F50 – 0x9F59 | 10 bytes |

**Structure per bank:**
- 10 bytes, each containing a preset number (1–75)
- `0x00` or `0xFF`: Empty slot

---

### SysEx Backup Configuration (0x9FE0 – 0x9FFF)

| Address | Content |
|---------|---------|
| 0x9FE0 – 0x9FFD | Backup data area (30 bytes) |
| 0x9FFE – 0x9FFF | End marker (0xFFFC) |

---

## Visual Memory Map

```
0x8000 ┌─────────────────────────────────────────────────────────┐
       │                    PRESET 1 (99 bytes)                  │
       │  [LNK0-31] [RLY] [PC1-16] [NAME] [SD] [SM] [SK1-4]      │
0x8063 ├─────────────────────────────────────────────────────────┤
       │                    PRESET 2 (99 bytes)                  │
       │                         ...                             │
0x9CAF ├─────────────────────────────────────────────────────────┤
       │                    PRESET 75 (99 bytes)                 │
0x9CEF ├─────────────────────────────────────────────────────────┤
0x9D0F │  RC (Relay Config)                                      │
0x9D10 ├─────────────────────────────────────────────────────────┤
       │              INPUT NAMES (IN1-IN16, 128 bytes)          │
0x9D90 ├─────────────────────────────────────────────────────────┤
       │             OUTPUT NAMES (OUT1-OUT16, 128 bytes)        │
0x9E0F ├─────────────────────────────────────────────────────────┤
0x9E10 │  OMC1-OMC16 (Output MIDI Channels, 16 bytes)            │
0x9E1F ├─────────────────────────────────────────────────────────┤
0x9E20 │  SMC │ PST │ IP │ PMS │ LB │ CTR0 │ CTR1 │ RESERVED     │
0x9E2F ├─────────────────────────────────────────────────────────┤
0x9E30 │              BANKS CONFIG (20 × 10 = 200 bytes)         │
0x9F5F ├─────────────────────────────────────────────────────────┤
       │                    RESERVED / BACKUP                    │
0x9FDF ├─────────────────────────────────────────────────────────┤
0x9FE0 │              SYSEX BACKUP CONFIG (30 bytes)             │
0x9FFD ├─────────────────────────────────────────────────────────┤
0x9FFE │  BACKUP END MARKER (0xFFFC)                             │
0x9FFF └─────────────────────────────────────────────────────────┘
```

---

## EEPROM Access

### Read Operation

```c
uint8_t ee_read(uint16_t offset) {
    return XB(EE_BASE + offset);
}
```

### Write Operation

```c
void ee_write(uint16_t offset, uint8_t data) {
    XB(EE_BASE + offset) = data;
}
```

### Block Write (with page boundary handling)

```c
void ee_write_data(uint16_t offset, const uint8_t *src, uint8_t length) {
    while (length) {
        // Write up to 32 bytes (page boundary)
        uint8_t n = 32u - (uint8_t)(offset & 31u);
        if (n > length) n = length;
        
        for (uint8_t i = 0; i < n; i++) {
            XB(EE_BASE + offset + i) = src[i];
        }
        
        // Poll DATA# until write completes
        uint8_t last = src[n - 1];
        while ((XB(EE_BASE + offset + n - 1) ^ last) & 0x80);
        
        offset += n;
        src += n;
        length -= n;
    }
}
```

---

## Example: Calculate Preset Address

```c
// Get EEPROM address for preset number N (1-75)
uint16_t get_preset_addr(uint8_t preset) {
    return (uint16_t)(preset - 1u) * 99u;
}

// Example: Preset 5
// Address = 0x8000 + (5 - 1) × 99 = 0x8000 + 396 = 0x818C
```

---

## Example: Parse Link Data

```c
// Read link 0 attenuation and routing from preset at ee_offset
uint8_t att = ee_read(ee_offset + 0x00);
uint8_t route = ee_read(ee_offset + 0x01);

// Extract gain levels
uint8_t start_level = att & 0x0F;      // Lower 4 bits
uint8_t end_level = (att >> 4) & 0x0F; // Upper 4 bits

// Extract routing
uint8_t input_num = route & 0x0F;      // Lower 4 bits
uint8_t output_num = (route >> 4) & 0x0F; // Upper 4 bits

// Convert to DAC value using gain table
uint8_t dac_value = gain_table[start_level];
```

---

## Related Documentation

- [README.md](README.md) — Main project documentation
- [MIDI Implementation](#midi-implementation) — MIDI message specifications
- [Gain Engine](#gain-engine) — Morphing algorithm details

---

**Last Updated:** 2026-10-24  
**Author:** Ivan Nuvoli
