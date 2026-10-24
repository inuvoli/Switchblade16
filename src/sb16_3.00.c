/*
 * ============================================================================
 * SOUNDSCULPTURE SWITCHBLADE 16 - MIDI AUDIO MATRIX SWITCHER FIRMWARE
 * ============================================================================
 * 
 * Author:		    Ivan Nuvoli (Nuvoli Custom Audio)
 * Date:		    2024-06-15
 * Version:	        2.0.1
 * 
 * Target Hardware: 80C31 (MCS-51 family) @ 12 MHz
 * Compiler:        SDCC (Small Device C Compiler)
 * Memory Model:    Small (128-byte IRAM, 512-byte XRAM @ 0x7E00)
 * Compilation:     sdcc -mmcs51 --model-small --iram-size 128 --xram-loc 0x7E00 --xram-size 0x200 --code-size 0x8000 firmware.c
 * 
 * ============================================================================
 */

#include <8051.h>
#include <stdint.h>

/* ============================================================================
 * MACRO DEFINITIONS
 * ============================================================================ */

/**
 * XB - eXternal Byte access macro
 * Provides convenient access to memory-mapped hardware registers in XDATA space
 */
#define XB(addr)  (*(__xdata volatile uint8_t *)(addr))

/* ============================================================================
 * MEMORY-MAPPED HARDWARE REGISTERS
 * ============================================================================ */

/* --- LCD Display (HD44780 compatible, 20 characters x 2 rows) --- */
#define LCD_CMD     0x1E00u    /**< LCD command register (write) */
#define LCD_DATA    0x1E01u    /**< LCD data register (read/write) */
#define LCD_STAT    0x1E02u    /**< LCD status register (read: D7 = busy flag) */

/* --- Front Panel LEDs (active low: 0 = ON, 0xFF = OFF) --- */
#define LED_PRG     0x1E10u    /**< PROGRAM mode indicator */
#define LED_PLAY    0x1E11u    /**< PLAY mode indicator */
#define LED_CNT     0x1E12u    /**< Continuous controller indicator */
#define LED_ON      0x00u      /**< LED ON value (active low) */
#define LED_OFF     0xFFu      /**< LED OFF value (active low) */

/* --- Relay Control (8 latching relays, one per output) --- */
#define RELAY_BASE  0x1E08u    /**< Base address for 8 relay latches */
#define RELAY_OPEN  0x00       /**< Relay Status: Close = 0 */ 
#define RELAY_CLOSE 0x01       /**< Relay Status: Open = 0 */ 

/* --- Audio Level Monitor Circuitry (Select which input is compared with the reference voltage set by SBSR.4 --- */
#define MON_A0      0x1E13u    /**< Level-monitor mux address (bit1) */
#define MON_A1      0x1E14u    /**< Level-monitor mux address (bit2) */
#define MON_A2      0x1E15u    /**< Level-monitor mux address (bit3) */
#define MON_EN0     0x1E16u    /**< Monitor enable channel 0-7  --- Active Low*/
#define MON_EN1     0x1E17u    /**< Monitor enable channel 8-15 --- Active Low */

/* --- Switchblade Control Register (SBSR) - Write only */
/* --- bit0: Out Buffer Address (Used to select which output to be muted)*/
/* --- bit1: Out Buffer Address (counted from 0 to 15) */
/* --- bit2: Out Buffer Address (the selected output is then muted setting OUT_MUTE to 0)*/
/* --- bit3: Out Buffer Address (if OUT_MUTE is set to 1 all output are active)*/
/* --- bit4: Input Vref Comparator Value (0 = 0V, 1 = 2,5V)  */
/* --- bit5: Input 1 6dB PAD (0 = Pad OFF, 1 = Pad ON)  */
/* --- bit6: Unused  */
/* --- bit7: Unused  */
#define SBSR        0x1E18u

/* --- DAC Chips (MP7529B, 8-bit multiplying DACs) --- */
#define DAC_LO      0x3600u    /**< DAC address space for links 0-15 */
#define DAC_HI      0x2E00u    /**< DAC address space for links 16-31 */

/* --- Audio Matrix Crosspoint Switches (CD74HC22106, 8x8 matrices) --- */
#define MX_OUT      0x3A00u    /**< Output matrix bank base address*/
#define MX_IN       0x3C00u    /**< Input matrix bank base address */

/* --- Configuration & Preset Storage (8 KB parallel EEPROM) --- */
#define EE_BASE     0x8000u    /**< EEPROM base address */

/* ============================================================================
 * PORT 3 CONTROL LINES (Bit-addressable I/O)
 * ============================================================================ */
#define DAC_AB_SELECT   P3_2       /**< Selects DAC channel A/B */
#define OUT_MUTE        P3_3       /**< Output buffer mute control (Ouput selected by SBSR.0-SBSR.3: 0 = Muted, 1 = All Active) */
#define MX_WR_ENABLE    P3_4       /**< Matrix Write Enable (active low) */
#define DAC_WR_ENABLE   P3_5       /**< DAC Write Enable (active high) */
#define MON_IN_STATE    P1_1       /**< Monitored Input State (0 = below Vref, 1 = above Vref) */

/* ============================================================================
 * PORT 1 - Front Panel Controls (all inputs, active low)
 * ============================================================================ */
#define KEY_MODE      (1u << 4)
#define KEY_UP        (1u << 5)
#define KEY_DOWN      (1u << 6)
#define KEY_SELECT    (1u << 7)
#define KEY_PRESSED   1u
#define KEY_RELEASED  0u

/* ============================================================================
 * EEPROM MEMORY LAYOUT
 * ============================================================================ */
#define NUM_PRESETS     75u
#define PRESET_LEN      99u
#define MAX_BANKS       20u

/* Preset fields offsets */
#define REC_LINKS       0x00u    /**< [64 byte] 32 x {attenuation (1 byte), routing (1 byte)} */
                                 /**< attenuation: bit0..3 set set input attenuation, bit4..7 set output attenuation                   */
                                 /**< ------------------------------------------------------------------------------------------------ */
								 /**< | 0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  | 10  | 11  | 12  | 13  | 14  | 15  | */
                                 /**< |OFF |-42dB|-36dB|-30dB|-27dB|-24dB|-21dB|-18dB|-15dB|-12dB| -9dB| -6dB| -3dB|  0dB| +3dB| +6dB| */
                                 /**< ------------------------------------------------------------------------------------------------ */
                                 /**< routing:     bit0..3 set input number (0..15) , bit4..7 set output number (0..15)                */

#define REC_RELAY       0x40u    /**< [1 byte]  Relay states (8 bits), each relay is mapped on a single bit. 0 = Open, 1 = Closed. bit0 = RLY1, bit7 = RLY8 */
#define REC_PC          0x41u    /**< [16 byte] Program Change numbers (0..127), one byte for each output device */
#define REC_NAME        0x51u    /**< [12 byte] Preset name (12 characters) */
#define REC_SWEEPDELAY  0x5Du    /**< [1 byte]  Preset Sweep delay value, from 250ms to 12950ms in 50ms step [0x01 = 250ms, 0xff = 12950ms] */
#define REC_SWEEPMODE   0x5Eu    /**< [1 byte]  Sweep mode: 0=off, 1=smooth, 2=multi-pan */
#define REC_CTRMASK     0x5Fu    /**< [4 byte]  bit mask for CTR1-controlled links (INFERRED)*/

/* Configuration & Global Settings */
#define CFG_RC          0x1D0Fu  /**< [1 byte]  Relay momentary/continuous mask */
#define NAMES_BASE      0x1D10u  /**< [256 byte] 32*832 x 8-char I/O names, IN names from 0x1D10u to 0x1D8f, OUT names from 0x1D90u to 0x1E0Fu */
#define CFG_OMC         0x1E10u  /**< [16 byte] Output MIDI channel assignments, one byte for each output device */
#define CFG_SMC         0x1E20u  /**< [1 byte]  SwitchBlade receive MIDI channel */
#define CFG_PST         0x1E21u  /**< [1 byte]  Preset switch time, 0ms - 1000ms in 100ms increments [ 0x0 = 0ms, 0xa = 1000 ms] */
#define CFG_PAD         0x1E22u  /**< [1 byte]  Input 1 pad attenuator, (0x0 = OFF, 0x01 = ON) */
#define CFG_CC_ON       0x1E23u  /**< [1 byte]  Program Change mode */
#define CFG_BNK_N       0x1E24u  /**< [1 byte]  Index of last populated bank */
#define CFG_CTR0        0x1E25u  /**< [1 byte]  Controller 0 CC number */
#define CFG_CTR1        0x1E26u  /**< [1 byte]  Controller 1 CC number */
#define CFG_BANKS       0x1E30u  /**< [20 byte??] 20 banks x 10 items */

/* MIDI SysEx Backup Configuration */
#define BLK_GROUPS      28u
#define BACKUP_END      0x1FFEu  /**< [2 byte]  End address of backup area */

/* ============================================================================
 * LOOKUP TABLES
 * ============================================================================ */

/**
 * Gain Table - Attenuation code to 8-bit DAC value mapping
 * 0x00 = mute, 0x80 = 0 dB, 0xFF = +6 dB
 */
static const uint8_t gain_tab[16] = {
    0x00, 0x01, 0x02, 0x04, 0x06, 0x08, 0x0B, 0x10,
    0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF
};

/* Gain level names for display */
static const char gain_name[16][4] = {
    "OFF", "-42", "-36", "-30", "-27", "-24", "-21", "-18",
    "-15", "-12", " -9", " -6", " -3", "  0", " +3", " +6"
};

/* Character set for naming (A-Z, 0-9, space) */
static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";

/* ============================================================================
 * GLOBAL STATE VARIABLES
 * ============================================================================ */

/* Front Panel State Variables */
static __bit key_repeated;                    /**< Key repeat flag for UP and DOWN keys */

/* Current Preset State (Internal RAM for fast access) */
static __data uint8_t cur_preset;             /**< Currently active preset (1-75) */
static __data uint8_t midi_ch;                /**< MIDI receive channel (0-15) */
static __data uint8_t pst_time;               /**< Preset transition time */
static __data uint8_t ctr0_cc;                /**< CC number for controller 0 */
static __data uint8_t ctr1_cc;                /**< CC number for controller 1 */
static __data uint8_t pad_bit;                /**< Input pad bit mask for SBSR.5 (0x20 = -6dB    , 0x00 = 0dB) */
static __data uint8_t vref_bit;               /**< Vref bit mask for SBSR.4      (0x10 = 2,5V    , 0x00 = 0V)  NOT IMPLEMENTER, ALWAYS ZERO. 2.5V VREF COULD BE USED FOR VUMETER*/
static __data uint8_t sweep_mode;             /**< Sweep mode */
static __data uint8_t rc_mask;                /**< Relay configuration mask (bitn = 0 -> S, bitn = 1 -> P)  */
static __data uint8_t ctr_mask[4];            /**< Link-to-controller mask */

/* Audio Link State (External RAM) */
static __xdata uint8_t cur_att[32];           /**< Current attenuation per link */
static __xdata uint8_t cur_route[32];         /**< Current routing per link */
static __xdata uint8_t cur_dac[32];           /**< Current DAC values per link */
static __xdata uint8_t tgt_dac[32];           /**< Target DAC values for fading */
static __xdata uint8_t pagebuf[32];           /**< EEPROM page buffer */

/* Interrupt-Shared Variables (must be volatile) */
static volatile __data uint8_t pos0;          /**< Sweep position 0 */
static volatile __data uint8_t pos1;          /**< Sweep position 1 */
static volatile __data uint8_t pc_value;      /**< Pending MIDI PC value */
static volatile __data uint8_t sweep_sd;      /**< Sweep timer reload */
static volatile __bit pc_pending;             /**< PC pending flag */
static volatile __bit pos_dirty;              /**< Position changed flag */
static volatile __bit sweep_up;               /**< Sweep direction */

/* Multi-pan (sweep mode 2): four independent triangle LFOs, channel = route & 3 */
static volatile __data uint8_t  pan_pos[4];   /**< Pan positions */
static volatile __data uint8_t  pan_dir;      /**< Direction bits, 1 = rising */
static volatile __data uint8_t  pan_exp;      /**< Channels whose period expired  */
static volatile __data uint16_t pan_cnt[4];   /**< Remaining ticks per channel */
static volatile __data uint16_t pan_last;     /**< Last programmed interval */
static __data uint16_t          pan_per;      /**< Base period P */
static __bit             relay_pulse;         /**< Relay pulse active */
static __bit             have_d1;             /**< MIDI data byte 1 received */
static __data uint8_t    m_status;            /**< MIDI status byte */
static __data uint8_t    m_d1;                /**< MIDI first data byte */

/* Programming Mode Buffer */
/* Remote mode: tables share blkbuf (the programming menu is never active
 * at the same time; the ISR only touches them while in_play is set). */
static __xdata uint8_t blkbuf[BLK_GROUPS * 7];
#define ed blkbuf /*Non serve ad un cazzo, da togliere */
#define REM_ATT   (blkbuf)                   /**< Working attenuation per link */
#define REM_RT    (blkbuf + 32)              /**< Working route per link */
#define REM_CC    (blkbuf + 64)              /**< CC number controlling each link (FF = none) */
#define REM_VAL   (blkbuf + 96)              /**< Last DAC value (CC<<1) per CC number */
#define SX_BUF    (blkbuf + 128)             /**< SysEx records: route, CC (32 pairs) */
static __bit  in_play;                       /**< Play mode active (remote/SysEx accepted) */
static __bit  rem_on;                        /**< Remote mode requested/active */
static __bit  rem_note_ev;                   /**< Note event pending */
static __bit  rem_cc_ev;                     /**< Remote CC event pending */
static __bit  sx_end;                        /**< SysEx load completed */
static __data uint8_t rem_note, rem_ccn, rem_ccv;
static __data uint8_t sx_stage, sx_cnt, sx_ph, sx_n1;

/* ============================================================================
 * UTILITY FUNCTIONS - Timing & Delays
 * ============================================================================ */

/**
 * delay_us10 - Busy-wait delay in 10-microsecond units
 */
static void delay_us10(uint8_t n)
{
    while (n--)
    {
        __asm nop __endasm; __asm nop __endasm; __asm nop __endasm;
        __asm nop __endasm; __asm nop __endasm; __asm nop __endasm;
    }
}

/**
 * delay_ms - Millisecond delay (approximate @ 12 MHz)
 */
static void delay_ms(uint16_t ms)
{
    while (ms--)
    {
        uint8_t i;
        for (i = 0; i < 100; i++)
        {
            delay_us10(1);
        }
    }
}

/* ============================================================================
 * EEPROM ACCESS FUNCTIONS
 * ============================================================================ */

static uint8_t ee_read(uint16_t offset)
{
    return XB(EE_BASE + offset);
}

static void ee_write(uint16_t offset, uint8_t data)
{
	XB(EE_BASE + offset) = data;
}

/**
 * ee_write_data - Write data to EEPROM with page-aware handling
 * EEPROM writes must respect 32-byte page boundaries.
 */
static void ee_write_data(uint16_t offset, const uint8_t *src, uint8_t length)
{
    while (length)
    {
        uint8_t n = 32u - (uint8_t)(offset & 31u);
        uint8_t i, last;
        
        if (n > length) n = length;
        
        for (i = 0; i < n; i++)
        {
            XB(EE_BASE + offset + i) = src[i];
        }
        
        /* Poll DATA# until write completes */
        last = src[n - 1];
        while ((XB(EE_BASE + offset + n - 1) ^ last) & 0x80);
        
        offset += n;
        src += n;
        length -= n;
    }
}

static uint16_t get_preset_addr(uint8_t preset)
{
    return (uint16_t)(preset - 1u) * PRESET_LEN;
}

/* ============================================================================
 * LCD DISPLAY DRIVER (HD44780, 20x2)
 * ============================================================================ */

/* Wait for operation to end, check pin D7 value*/
static void lcd_wait_busy(void)
{
    uint8_t t = 250;
    while ((XB(LCD_STAT) & 0x80) && --t);
}

/* Send a command to the LCD*/
static void lcd_cmd(uint8_t cmd)
{
    XB(LCD_CMD) = cmd;
    lcd_wait_busy();
}

/* Send a character to the LCD*/
static void lcd_putc(uint8_t c)
{
    XB(LCD_DATA) = c;
    lcd_wait_busy();
}

/* Init LCD Display*/
static void lcd_init(void)
{
    delay_ms(40); XB(LCD_CMD) = 0x30;
    delay_ms(5);  XB(LCD_CMD) = 0x30;
    delay_ms(1);  XB(LCD_CMD) = 0x30;
    delay_ms(1);
    lcd_cmd(0x38);  /* 8-bit, 2 lines */
    lcd_cmd(0x0C);  /* Display on */
    lcd_cmd(0x01);  /* Clear */
    delay_ms(2);
    lcd_cmd(0x06);  /* Entry mode */
}

/* Clear the screen */
static void lcd_clear(void)
{
    lcd_cmd(0x01);
}

/* Set the cursor position on the LCD*/
static void lcd_goto(uint8_t row, uint8_t col)
{
    lcd_cmd((row ? 0xC0 : 0x80) + col);
}

/* Write a terminated string on the LCD*/
static void lcd_puts(const char *s)
{
    while (*s) lcd_putc((uint8_t)*s++);
}

/* Write a terminated string at a specific cursor position on the LCD*/
static void lcd_string_at(uint8_t row, uint8_t col, const char *s)
{
    lcd_goto(row, col);
    lcd_puts(s);
}

/* Convert and Write a number on the LCD*/
static void lcd_num(uint8_t v)
{
    uint8_t d;

    if (v >= 100)
    {
        d = v / 100;           
        v %= 100;
        lcd_putc('0' + d);
        d = v / 10;             
        v %= 10;
        lcd_putc('0' + d);
    } else if (v >= 10)
    {
        d = v / 10;
        v %= 10;
        lcd_putc('0' + d);
    }
    lcd_putc('0' + v);
}

/* Convert and Write a number at a specific cursor position on the LCD*/
static void lcd_num_at(uint8_t row, uint8_t col, uint8_t v)
{
    lcd_goto(row, col);
    lcd_num(v);
}

/* Write Switch Blade Status on the LCD*/
static void lcd_status(void)
{
    uint8_t i;
    uint16_t addr = get_preset_addr(cur_preset);
    
    lcd_num_at(0,0,cur_preset);
    lcd_putc(' ');
    for (i = 0; i < 12; i++)
    {
        lcd_putc(ee_read(addr + REC_NAME + i));
    }
    lcd_puts("     ");

    lcd_string_at(1, 0, sweep_mode == 1 ? "SWEEP " : sweep_mode == 2 ? "PAN   " : "CTR   ");
    lcd_num(pos0);
    lcd_putc(' ');
    lcd_num(pos1);
    lcd_puts("       ");
}

/* ============================================================================
 * AUDIO MATRIX ENGINE
 * ============================================================================ */

 /**
 * mon_input_select - Select which input signal is compared to Vref, selected from SBSR.4 bit
 */
static void mon_input_select(uint8_t in)
{
    /* Set lower 3 bit*/
    XB(MON_A0) = in;
    XB(MON_A1) = in >> 1;
    XB(MON_A2) = in >> 2;
    
    if (in & 8) {
        XB(MON_EN0) = 0;
        XB(MON_EN1) = 1;
    } else {
        XB(MON_EN0) = 1;
        XB(MON_EN1) = 0;
    }
}

/**
 * mon_wait_edge - Wait for audio signal to cross Vref value set by SBSR.4
 * or for timeout. Prevents clicks by switching at low signal levels.
 */
static void mon_wait_edge(void)
{
    uint8_t a = 7;
    uint8_t b = 27;
    
    if (MON_IN_STATE)
    {
        while(1)
        {
            if (!MON_IN_STATE) return;
            if (--b == 0 && --a == 0) return;
        }
    } else
    {
        while(1)
        {
            if (MON_IN_STATE) return;
            if (--b == 0 && --a == 0) return;
        }
    }
}

/**
 * mx_offset - evaluate the offset from the Base Address for
 * input and output matrix according to the selected link [0, 31]
 * and the selected input or output [0, 15].
 */
static uint16_t mx_offset(uint8_t link, uint8_t io)
{
    return ((uint16_t)(link >> 4) << 8) | 
           ((uint16_t)(link & 0x0F) << 4) | io;
}

/**
 * mx_select_output - select the output to be muted updating SBSR
 * first four bit. The selected output is muted setting OUT_MUTE
 * to zero 
 */
static void mx_select_output(uint8_t out)
{
    XB(SBSR) = (out & 0x0F) | pad_bit | vref_bit;
}

/**
 * link_switch - set the switch of the selected Input and Output
 * matrix chip according to the specified Link and Route Value.
 * Link is the id of the selected Link [0. 31]
 * Route define the connected input and output:
 *   - bit 0..4: define the Input Number [0, 15]
 *   - bit 5..7: define the Output Number [0, 15]
 * State define if the switch is open (0) or closed (1)  
 */
static void link_switch(uint8_t link, uint8_t route, uint8_t state)
{
    uint8_t in  = route & 0x0F;
    uint8_t out = route >> 4;
    
    mx_select_output(out);
    mon_input_select(in);
    mon_wait_edge();
    
    OUT_MUTE = 0;
    MX_WR_ENABLE = 0;
    XB(MX_OUT + mx_offset(link, out)) = state;
    XB(MX_IN  + mx_offset(link, in))  = state;
    MX_WR_ENABLE = 1;
    delay_us10(1);
    OUT_MUTE = 1;
}

/**
 * dac_write - set the attenuation value for the specified link.
 * if mute is true the output selected by SBSR is muted before
 * change the attenuation value than is unmuted.
 */
static void dac_write(uint8_t link, uint8_t attn, __bit mute)
{
    uint16_t addr = ((link & 0x10) ? DAC_HI : DAC_LO) + ((link >> 1) & 7);
    
    DAC_AB_SELECT = link & 1;
    if (mute)
        OUT_MUTE = 0;   /*Mute selected output buffer*/
    
    DAC_WR_ENABLE = 1;
    XB(addr) = attn;
    DAC_WR_ENABLE = 0;
    /*DAC_AB_SELECT = 1;*/
    
    if (mute)
    {
        delay_us10(1);
        OUT_MUTE = 1;  /*Unmute all output buffers*/
    }
}

/**
 * dac_value - return an interpolated dac attenuation value
 * beetwen two values according to pos value. the two attenuation 
 * values are stored in attn. the upper value on bit4..7, the lower
 * value on the bit 0..3
 */
static uint8_t dac_value(uint8_t attn, uint8_t pos)
{
    uint8_t lo = gain_tab[attn & 0x0F];
    uint8_t hi = gain_tab[attn >> 4];
    
    if (lo == hi) return lo;
    if (lo < hi)  return lo + (uint8_t)(((uint16_t)(hi - lo) * pos) >> 8);
    return hi + (uint8_t)(((uint16_t)(lo - hi) * (uint8_t)~pos) >> 8);
}

static uint8_t link_pos(uint8_t link, uint8_t route)
{
    if (!(ctr_mask[link >> 3] & (1u << (link & 7)))) return pos0;
    return sweep_mode == 2 ? pan_pos[route & 3] : pos1;
}

static __bit sweeping(uint8_t att)
{
    return att != 0 && (att & 0x0F) != (att >> 4);
}

/**
 * ramp_dacs - Fade all DACs toward target values
 */
static void ramp_dacs(uint8_t ms_per_pass)
{
    uint8_t i, busy;
    
    do {
        busy = 0;
        for (i = 0; i < 32; i++) {
            uint8_t c = cur_dac[i], t = tgt_dac[i];
            if (c == t) continue;
            
            if (c < t) c = (t - c > 8) ? c + 8 : t;
            else       c = (c - t > 8) ? c - 8 : t;
            
            dac_write(i, c, 0);
            cur_dac[i] = c;
            busy = 1;
        }
        if (busy && ms_per_pass) delay_ms(ms_per_pass);
    } while (busy);
}

/* ============================================================================
 * RELAY CONTROL
 * ============================================================================ */

static void relays_write(uint8_t v)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        XB(RELAY_BASE + i) = v;
        v = (v >> 1) | (v << 7);
    }
    relay_pulse = 1;
}

static void relays_check_pulse(void)
{
    uint8_t i, m = rc_mask;
    
    if (!relay_pulse) return;
    
    delay_ms(20);
    relay_pulse = 0;
    
    for (i = 0; i < 8; i++) {
        if (m & 1) XB(RELAY_BASE + i) = 0;
        m = (m >> 1) | (m << 7);
    }
}

/* ============================================================================
 * MIDI OUTPUT
 * ============================================================================ */
static void midi_tx(uint8_t b)
{
    uint8_t t = 90;
    while (!TI && --t);
    TI = 0;
    SBUF = b;
}

static void midi_send_program_changes(uint16_t rec)
{
    uint8_t n;
    
    ES = 0;
    TI = 1;
    
    for (n = 0; n < 16; n++) {
        uint8_t ch = ee_read(CFG_OMC + n);
        uint8_t pc = ee_read(rec + REC_PC + n);
        
        if (!(ch & 0x80) && !(pc & 0x80)) {
            midi_tx(0xC0 | ch);
            midi_tx(pc);
        }
    }
    
    {
        uint8_t t = 90;
        while (!TI && --t);
        TI = 0;
    }
    ES = 1;
}

/* ============================================================================
 * PRESET RECALL ENGINE
 * ============================================================================ */

static void recall_preset(uint8_t n)
{
    uint8_t i;
    uint16_t rec;
    uint32_t route_changed = 0;
    
    if (n < 1 || n > NUM_PRESETS) return;
    
    cur_preset = n;
    rec = get_preset_addr(n);
    
    TR0 = 0; ET0 = 0;
    sweep_mode = ee_read(rec + REC_SWEEPMODE);
    for (i = 0; i < 4; i++) {
        ctr_mask[i] = ee_read(rec + REC_CTRMASK + i);
    }
    if (sweep_mode) { pos1 = 0; sweep_up = 1; }
    
    /* Update LED if preset has swept links */
    {
        uint8_t any = 0;
        for (i = 0; i < 32; i++) {
            if (sweeping(ee_read(rec + REC_LINKS + 2 * i))) any = 1;
        }
        XB(LED_CNT) = any ? 0 : 0xFF;
    }
    
    /* Phase 1: Calculate targets */
    for (i = 0; i < 32; i++) {
        uint8_t att   = ee_read(rec + REC_LINKS + 2 * i);
        uint8_t route = ee_read(rec + REC_LINKS + 2 * i + 1);
        
        if (cur_att[i] != 0 && (att == 0 || route != cur_route[i])) {
            route_changed |= 1ul << i;
        }
        tgt_dac[i] = (route_changed & (1ul << i)) 
                     ? 0 : (att ? dac_value(att, link_pos(i, route)) : 0);
    }
    ramp_dacs(pst_time ? (uint8_t)(pst_time * 100u / 32u) : 0);
    
    /* Phase 2: Switch crosspoints */
    for (i = 0; i < 32; i++) {
        uint8_t att   = ee_read(rec + REC_LINKS + 2 * i);
        uint8_t route = ee_read(rec + REC_LINKS + 2 * i + 1);
        uint8_t chg   = (route_changed & (1ul << i)) != 0;
        
        if (chg) link_switch(i, cur_route[i], 0);
        if (att != 0 && (cur_att[i] == 0 || chg)) link_switch(i, route, 1);
        
        cur_att[i] = att;
        cur_route[i] = route;
        tgt_dac[i] = att ? dac_value(att, link_pos(i, route)) : 0;
    }
    
    /* Phase 3: Fade up */
    ramp_dacs(pst_time ? (uint8_t)(pst_time * 100u / 32u) : 0);
    
    relays_write(ee_read(rec + REC_RELAY));
    midi_send_program_changes(rec);
    
    if (sweep_mode == 1) {
        sweep_sd = ee_read(rec + REC_SWEEPDELAY);
        TH0 = ~sweep_sd;
        TL0 = 0xFF;
        TR0 = 1; ET0 = 1;
    } else if (sweep_mode == 2) {
        uint8_t k;
        sweep_sd = ee_read(rec + REC_SWEEPDELAY);
        pan_per = (uint16_t)sweep_sd << 8;
        for (k = 0; k < 4; k++) {
            pan_pos[k] = 0;
            pan_cnt[k] = pan_per * (k + 1);
        }
        pan_dir = 0x0F;
        pan_exp = 0;
        pan_last = pan_per;
        TH0 = ~sweep_sd;
        TL0 = 0xFF;
        TR0 = 1; ET0 = 1;
    }
    pos_dirty = 1;
}

static void update_continuous(void)
{
    uint8_t i;
    for (i = 0; i < 32; i++) {
        uint8_t v;
        if (!sweeping(cur_att[i])) continue;
        
        v = dac_value(cur_att[i], link_pos(i, cur_route[i]));
        if (v == cur_dac[i]) continue;
        
        mon_wait_edge();
        dac_write(i, v, 0);
        cur_dac[i] = v;
    }
}

/* ============================================================================
 * INTERRUPT SERVICE ROUTINES
 * ============================================================================ */

void timer0_isr(void) __interrupt(1)
{
    TR0 = 0;
    
    if (sweep_mode == 2) {
        uint8_t k, b;
        uint16_t m;
        
        /* step channels flagged by the previous interrupt, then flag expired ones */
        for (k = 0, b = 1; k < 4; k++, b <<= 1) {
            if (!(pan_exp & b)) continue;
            if (pan_dir & b) {
                if (pan_pos[k] == 0xFF) { pan_dir &= ~b; pan_pos[k] -= 3; }
                else pan_pos[k] += 3;
            } else {
                if (pan_pos[k] == 0) { pan_dir |= b; pan_pos[k] += 3; }
                else pan_pos[k] -= 3;
            }
        }
        pan_exp = 0;
        for (k = 0, b = 1; k < 4; k++, b <<= 1) {
            pan_cnt[k] -= pan_last;
            if (pan_cnt[k] == 0) {
                pan_cnt[k] = pan_per * (k + 1);
                pan_exp |= b;
            }
        }
        m = pan_cnt[0];
        for (k = 1; k < 4; k++)
            if (pan_cnt[k] < m) m = pan_cnt[k];
        pan_last = m;
        m = ~m;
        TH0 = m >> 8;
        TL0 = (uint8_t)m;
        pos_dirty = 1;
        TR0 = 1;
        return;
    }
    
    TH0 = ~sweep_sd;
    TL0 = 0xFF;
    
    if (!sweep_up) {
        if (pos1) pos1 -= 3;
        else { sweep_up = 1; pos1 += 3; }
    } else {
        if (pos1 != 0xFF) pos1 += 3;
        else { sweep_up = 0; pos1 -= 3; }
    }
    
    pos_dirty = 1;
    TR0 = 1;
}

void serial_isr(void) __interrupt(4)
{
    uint8_t b;
    if (!RI) return;
    
    RI = 0;
    b = SBUF;
    
    if (b & 0x80) {
        if ((b & 0xF8) == 0xF8) return;
        if (b == 0xF7 && sx_stage == 3 && sx_cnt) sx_end = 1;
        sx_stage = (b == 0xF0 && in_play) ? 1 : 0;
        m_status = b;
        have_d1 = 0;
        return;
    }
    
    /* SysEx: 0C <cmd=2> then triplets (out, in, CC); bytes > 0F are dropped */
    if (m_status == 0xF0) {
        if (sx_stage == 1) sx_stage = (b == 0x0C) ? 2 : 0;
        else if (sx_stage == 2) {
            if (b == 2) { sx_stage = 3; sx_cnt = 0; sx_ph = 0; rem_on = 1; }
            else sx_stage = 0;
        } else if (sx_stage == 3 && !(b & 0xF0) && sx_cnt < 32) {
            if (sx_ph == 0) { sx_n1 = b; sx_ph = 1; }
            else if (sx_ph == 1) { SX_BUF[sx_cnt * 2] = (sx_n1 << 4) | b; sx_ph = 2; }
            else { SX_BUF[sx_cnt * 2 + 1] = b; sx_cnt++; sx_ph = 0; }
        }
        return;
    }
    
    if ((m_status & 0x80) == 0 || (m_status & 0x0F) != midi_ch) return;
    
    switch (m_status & 0xF0) {
        case 0x90:
            if (!have_d1) { m_d1 = b; have_d1 = 1; }
            else {
                have_d1 = 0;
                if (b && in_play) { rem_note = m_d1; rem_note_ev = 1; rem_on = 1; }
            }
            break;
        case 0xC0:
            pc_value = b; pc_pending = 1;
            break;
        case 0xD0:
            pos0 = b << 1; pos_dirty = 1;
            break;
        case 0xE0:
            if (!have_d1) have_d1 = 1;
            else { pos0 = b << 1; pos_dirty = 1; have_d1 = 0; }
            break;
        case 0xB0:
            if (!have_d1) { m_d1 = b; have_d1 = 1; }
            else {
                have_d1 = 0;
                if (rem_on) { rem_ccn = m_d1; rem_ccv = b; rem_cc_ev = 1; }
                else if (m_d1 == ctr0_cc) { pos0 = b << 1; pos_dirty = 1; }
                else if (m_d1 == ctr1_cc && !sweep_mode) { pos1 = b << 1; pos_dirty = 1; }
            }
            break;
    }
}

/* ============================================================================
 * SYSTEM INITIALIZATION
 * ============================================================================ */

static void clear_matrices_and_dacs(void)
{
    uint16_t a;
    uint8_t i;
    
    XB(LED_PRG) = 0xFF; XB(LED_PLAY) = 0; XB(LED_CNT) = 0xFF;
    MX_WR_ENABLE = 0;
    for (a = 0; a < 0x400; a++) XB(MX_OUT + a) = 0;
    MX_WR_ENABLE = 1;
    
    for (i = 0; i < 32; i++) {
        dac_write(i, 0, 0);
        cur_dac[i] = 0; cur_att[i] = 0; cur_route[i] = 0;
    }
    OUT_MUTE = 1;
}

static void factory_reset(void)
{
    uint8_t p, i;
    uint16_t a = 0;
    static const char tail[6] = { 1, 0, 0, 0, 0, 0 };
    
    lcd_string_at(0, 0, "FACTORY RESET...    ");
    
    for (p = 1; p <= NUM_PRESETS; p++) {
        for (i = 0; i < 32; i++) pagebuf[i] = 0;
        ee_write_data(a, pagebuf, 32);
        ee_write_data(a + 32, pagebuf, 32);
        
        pagebuf[0] = 0;
        ee_write_data(a + REC_RELAY, pagebuf, 1);
        
        for (i = 0; i < 16; i++) pagebuf[i] = 0xFF;
        ee_write_data(a + REC_PC, pagebuf, 16);
        
        {
            static const char nm[] = "NONAMEYET ";
            for (i = 0; i < 10; i++) pagebuf[i] = nm[i];
            pagebuf[10] = '0' + p / 10;
            pagebuf[11] = '0' + p % 10;
            ee_write_data(a + REC_NAME, pagebuf, 12);
        }
        
        for (i = 0; i < 6; i++) pagebuf[i] = tail[i];
        ee_write_data(a + REC_SWEEPDELAY, pagebuf, 6);
        a += PRESET_LEN;
    }
    
    for (i = 0; i < 32; i++) pagebuf[i] = 0x20;
    for (a = NAMES_BASE; a < NAMES_BASE + 256; a += 32) {
        ee_write_data(a, pagebuf, 32);
    }
    
    for (i = 0; i < 17; i++) pagebuf[i] = 0xFF;
    ee_write_data(CFG_OMC, pagebuf, 17);
    
    for (i = 0; i < 32; i++) pagebuf[i] = 0;
    for (a = 0x1E30; a < 0x1E30 + 288; a += 32) {
        ee_write_data(a, pagebuf, 32);
    }
    
    ee_write(CFG_RC, 0);
    ee_write(CFG_SMC, 0);
    ee_write(CFG_PST, 0);
    ee_write(CFG_PAD, 0);
    ee_write(CFG_CC_ON, 1);
    ee_write(0x1E24, 0);
    ee_write(CFG_CTR0, 7);
    ee_write(CFG_CTR1, 0);
    ee_write(0x1E30, 1);
}

static void load_config(void)
{
    midi_ch  = ee_read(CFG_SMC) & 0x0F;
    pst_time = ee_read(CFG_PST);
    pad_bit  = ee_read(CFG_PAD) ? 0x20 : 0x00;
    vref_bit = 0x00; /*NOT USED*/
    ctr0_cc  = ee_read(CFG_CTR0);
    ctr1_cc  = ee_read(CFG_CTR1);
    rc_mask  = ee_read(CFG_RC);
}

static void peripherals_init(void)
{
    TMOD = 0x21;
    TH1  = 0xFE;
    PCON = 0x80;
    SCON = 0x50;
    TR1  = 1;
    IP   = 0x10;
    ES   = 1;
    EA   = 1;
}

/* ============================================================================
 * DIAGNOSTIC ROUTINES
 * ============================================================================ */

static void patch_set(uint8_t link, uint8_t n, uint8_t on)
{
    MX_WR_ENABLE = 0;
    XB(MX_IN  + mx_offset(link, n)) = on;
    XB(MX_OUT + mx_offset(link, n)) = on;
    MX_WR_ENABLE = 1;
}

static void self_test(void)
{
    uint16_t p, i;
    uint8_t ok = 1, aborted = 0, in = 0, link, v;
    
    mon_input_select(0);
    XB(SBSR) = 0;
    
    /* RAM Test */
    lcd_string_at(0, 0, "RAM TEST:           ");
    lcd_string_at(1, 0, "                    ");
    
    for (p = 0; p < 512 && ok && !aborted; p++) {
        for (i = 0; i < 512; i++) {
            XB(0x7E00u + i) = (i == p) ? 0xAA : 0x55;
        }
        for (i = 0; i < 512; i++) {
            if (XB(0x7E00u + i) != ((i == p) ? 0xAA : 0x55)) {
                ok = 0; break;
            }
        }
        if (ok && !P1_4) aborted = 1;
    }
    
    if (!aborted) {
        lcd_string_at(1, 0, ok ? "PASS" : "FAIL");
        while (P1_4);
    }
    
    /* Patch Test */
    lcd_string_at(0, 0, "PATCH TEST:         ");
    lcd_string_at(1, 0, "IN/OUT->     D/A->  ");
    
    for (;;) {
        mon_input_select(in);
        lcd_goto(1, 8); lcd_num(in + 1);
        link = 0;
        
        for (;;) {
            lcd_goto(1, 18); lcd_num(link + 1);
            patch_set(link, in, 1);
            
            for (;;) {
                for (v = 255; v; v--) {
                    dac_write(link, v, 0);
                    delay_us10(10);
                }
                dac_write(link, 0, 0);
                
                if (!P1_6) {
                    patch_set(link, in, 0);
                    in = (in + 1) & 15;
                    goto next_input;
                }
                if (!P1_4) goto final_stage;
                if (!P1_5) break;
            }
            
            patch_set(link, in, 0);
            if (++link == 32) {
                link = 0;
                lcd_goto(1, 8);
                lcd_num(in + 1);
            }
        }
    next_input:;
    }
    
final_stage:
    lcd_string_at(0, 0, "HI GAIN & PEAK TEST ");
    lcd_string_at(1, 0, "                    ");
    mon_input_select(0);
    XB(SBSR) = 0x30;
    dac_write(0, 0xFF, 0);
    for (;;) ;
}

/* ============================================================================
 * FRONT PANEL INTERFACE
 * ============================================================================ */

static uint8_t read_buttons(void)
{
    return (uint8_t)~P1 & 0xf0;
}

static void change_preset_by(int8_t d)
{
    uint8_t n = cur_preset + d;
    if (n < 1) n = NUM_PRESETS;
    if (n > NUM_PRESETS) n = 1;
    recall_preset(n);
    lcd_status();
}

static uint8_t get_key_pressed(void)
{
    uint8_t key;
    uint8_t key_delay;
    uint8_t n;
    
	/* Wait for a key press and debounce */
    key = read_buttons();
    if (key)
    {
        delay_ms(20);
        if (read_buttons() != key)
            return 0;
    }
    else
    {
		return 0;
    }
	
    /* Determine which key was pressed */
    if (key & KEY_MODE) key = KEY_MODE;
    else if (key & KEY_UP) key = KEY_UP;
    else if (key & KEY_DOWN) key = KEY_DOWN;
    else key = KEY_SELECT;
    
    /* Handle key repeat for UP and DOWN keys */
    if (key == KEY_UP || key == KEY_DOWN)
    {
        key_delay = key_repeated ? 3 : 20;
        for (n = 0; n < key_delay && (read_buttons() & key); n++)
        {
            delay_ms(20);
        }
        key_repeated = (n == key_delay);
        return key;
    }
    
    /* Wait for key release and debounce */
    while (read_buttons() & key);
    key_repeated = 0;
    delay_ms(20);
    return key;
}

static uint8_t wrap_step(uint8_t v, uint8_t k, uint8_t lo, uint8_t hi)
{
    if (k == KEY_UP) return v >= hi ? lo : v + 1;
    return v <= lo ? hi : v - 1;
}

static uint8_t char_step(uint8_t c, uint8_t k)
{
    uint8_t i = 36, n;
    for (n = 0; n < 37; n++) {
        if ((uint8_t)charset[n] == c) { i = n; break; }
    }
    i = (k == KEY_UP) ? (i == 36 ? 0 : i + 1) : (i == 0 ? 36 : i - 1);
    return (uint8_t)charset[i];
}

static __bit name_blank(uint8_t slot)
{
    uint8_t i, c;
    for (i = 0; i < 8; i++) {
        c = ee_read(NAMES_BASE + (uint16_t)slot * 8 + i);
        if (c != ' ' && c != 0xFF && c != 0) return 0;
    }
    return 1;
}

static void show_name(uint8_t slot)
{
    uint8_t i;
    if (name_blank(slot)) {
        lcd_puts(slot < 16 ? "IN  " : "OUT ");
        lcd_num((slot & 15) + 1);
        lcd_puts("  ");
    } else {
        for (i = 0; i < 8; i++) {
            lcd_putc(ee_read(NAMES_BASE + (uint16_t)slot * 8 + i));
        }
    }
}

static uint8_t slot_step(uint8_t base, uint8_t cur, uint8_t k)
{
    uint8_t i, n = cur, all = 1;
    
    for (i = 0; i < 16; i++) {
        if (!name_blank(base + i)) all = 0;
    }
    
    for (i = 0; i < 16; i++) {
        n = (k == KEY_UP) ? ((n + 1) & 15) : ((n - 1) & 15);
        if (all || !name_blank(base + n)) return n;
    }
    return cur;
}

/* ============================================================================
 * PRESET EDITOR SCREENS
 * ============================================================================ */

static void scr_name(void)
{
    static __xdata uint8_t pos, k, i;
    pos = 0;
    
    lcd_string_at(0, 0, "PRESET NAME         ");
    lcd_string_at(1, 0, "                    ");
    
    for (;;) {
        lcd_goto(1, 0);
        for (i = 0; i < 12; i++) {
            lcd_putc(ed[REC_NAME + i]);
        }
        lcd_goto(1, pos);
        
        k = get_key_pressed();
        if (k == KEY_MODE) return;
        if (k == KEY_SELECT) {
            pos = (pos == 11) ? 0 : pos + 1;
        } else {
            ed[REC_NAME + pos] = char_step(ed[REC_NAME + pos], k);
        }
    }
}

static uint8_t find_link(uint8_t in, uint8_t out)
{
    uint8_t i, r = (out << 4) | in;
    for (i = 0; i < 32; i++) {
        if (ed[2 * i] && ed[2 * i + 1] == r) return i;
    }
    return 0xFF;
}

static uint8_t free_link(void)
{
    uint8_t i;
    for (i = 0; i < 32; i++) {
        if (ed[2 * i] == 0) return i;
    }
    return 0xFF;
}

static void scr_patch(void)
{
    static const uint8_t fcol[5] = { 0, 12, 6, 14, 19 };
    static __xdata uint8_t in, out, f, lk, k, a, n;
    
    in = 0; out = 0; f = 0;
    lcd_string_at(0, 0, "                    ");
    lcd_string_at(1, 0, "                    ");
    lk = find_link(in, out);
    
    for (;;) {
        a = (lk == 0xFF) ? 0 : ed[2 * lk];
        
        lcd_goto(0, 0);
        show_name(in);
        lcd_puts(" -> ");
        show_name(16 + out);
        
        lcd_goto(1, 0);
        lcd_puts("START ");
        lcd_puts(gain_name[a & 15]);
        lcd_puts(" END ");
        lcd_puts(gain_name[a >> 4]);
        lcd_puts(" C");
        lcd_putc(lk == 0xFF ? '-' : 
                 ((ed[REC_CTRMASK + (lk >> 3)] >> (lk & 7)) & 1) ? '1' : '0');
        
        lcd_goto(f >= 2, fcol[f]);
        
        k = get_key_pressed();
        if (k == KEY_MODE) return;
        if (k == KEY_SELECT) {
            f = (f == 4) ? 0 : f + 1;
            continue;
        }
        
        if (f == 0) {
            in = slot_step(0, in, k);
            lk = find_link(in, out);
        } else if (f == 1) {
            out = slot_step(16, out, k);
            lk = find_link(in, out);
        } else if (f == 4) {
            if (lk != 0xFF) {
                ed[REC_CTRMASK + (lk >> 3)] ^= (uint8_t)(1u << (lk & 7));
            }
        } else {
            n = (f == 2) ? (a & 15) : (a >> 4);
            n = (k == KEY_UP) ? ((n + 1) & 15) : ((n - 1) & 15);
            a = (f == 2) ? ((a & 0xF0) | n) : ((a & 0x0F) | (n << 4));
            
            if (a == 0) {
                if (lk != 0xFF) {
                    ed[2 * lk] = 0;
                    ed[2 * lk + 1] = 0;
                    lk = 0xFF;
                }
            } else {
                if (lk == 0xFF) {
                    lk = free_link();
                    if (lk == 0xFF) {
                        lcd_string_at(1, 0, "NO FREE LINK        ");
                        delay_ms(800);
                        continue;
                    }
                    ed[2 * lk + 1] = (out << 4) | in;
                    ed[REC_CTRMASK + (lk >> 3)] &= (uint8_t)~(1u << (lk & 7));
                }
                ed[2 * lk] = a;
            }
        }
    }
}

static void scr_midi(void)
{
    static __xdata uint8_t out, f, k, v, i, any;
    
    out = 0; f = 0; any = 0;
    for (i = 0; i < 16; i++) {
        if (!(ee_read(CFG_OMC + i) & 0x80)) {
            if (!any) out = i;
            any = 1;
        }
    }
    if (!any) return;
    
    lcd_string_at(0, 0, "MIDI OUT            ");
    lcd_string_at(1, 0, "PROGRAM CHANGE ->   ");
    
    for (;;) {
        v = ed[REC_PC + out];
        
        lcd_goto(0, 12);
        show_name(16 + out);
        lcd_goto(1, 17);
        
        if (v & 0x80) lcd_puts("OFF");
        else lcd_num(v);
        
        if (f) lcd_goto(1, 19);
        else lcd_goto(0, 12);
        
        k = get_key_pressed();
        if (k == KEY_MODE) return;
        if (k == KEY_SELECT) {
            f ^= 1;
            continue;
        }
        
        if (f == 0) {
            do {
                out = (k == KEY_UP) ? ((out + 1) & 15) : ((out - 1) & 15);
            } while (ee_read(CFG_OMC + out) & 0x80);
        } else if (k == KEY_UP) {
            ed[REC_PC + out] = (v & 0x80) ? 0 : (v == 127 ? 0xFF : v + 1);
        } else {
            ed[REC_PC + out] = (v & 0x80) ? 127 : (v == 0 ? 0xFF : v - 1);
        }
    }
}

static uint8_t edit_bits(const char *l1, const char *l2, uint8_t v, 
                         char on, char off, __bit live)
{
    static __xdata uint8_t f, i, k;
    f = 0;
    
    lcd_string_at(0, 0, l1);
    lcd_string_at(1, 0, l2);
    
    for (;;) {
        for (i = 0; i < 8; i++) {
            lcd_goto(i >> 2, 10 + 3 * (i & 3));
            lcd_putc(((v >> i) & 1) ? on : off);
        }
        lcd_goto(f >> 2, 10 + 3 * (f & 3));
        
        k = get_key_pressed();
        if (k == KEY_MODE) return v;
        if (k == KEY_SELECT) {
            f = (f + 1) & 7;
        } else {
            v ^= (uint8_t)(1u << f);
            if (live) relays_write(v);
        }
    }
}

static void scr_sweep(void)
{
    static const char mode_txt[3][13] = {
        "OFF         ", "SMOOTH SWEEP", "MULTI PAN   "
    };
    static __xdata uint8_t f, k, m, sd;
    
    f = 0;
    m = ed[REC_SWEEPMODE];
    sd = ed[REC_SWEEPDELAY];
    if (m > 2) m = 0;
    if (sd == 0) sd = 1;
    
    lcd_string_at(0, 0, "AUTOSWEEP PARAMETERS");
    lcd_string_at(1, 0, "                    ");
    
    for (;;) {
        lcd_goto(1, 0);
        lcd_puts(mode_txt[m]);
        lcd_goto(1, 17);
        lcd_num(sd);
        lcd_goto(1, f ? 19 : 0);
        
        k = get_key_pressed();
        if (k == KEY_MODE) break;
        if (k == KEY_SELECT) {
            f ^= 1;
        } else if (f == 0) {
            m = wrap_step(m, k, 0, 2);
        } else if (k == KEY_UP) {
            if (sd < 255) sd++;
        } else {
            if (sd > 1) sd--;
        }
    }
    
    ed[REC_SWEEPMODE] = m;
    ed[REC_SWEEPDELAY]  = sd;
}

static void menu_preset(void)
{
    uint16_t rec = get_preset_addr(cur_preset);
    uint8_t i;
    
    for (i = 0; i < PRESET_LEN; i++) {
        ed[i] = ee_read(rec + i);
    }
    
    scr_name();
    scr_patch();
    scr_midi();
    ed[REC_RELAY] = edit_bits(
        "CONTROL  1  2  3  4 ",
        "OUTPUTS  5  6  7  8 ",
        ed[REC_RELAY], '+', '-', 1
    );
    scr_sweep();
    ee_write_data(rec, ed, PRESET_LEN);
}

/* ============================================================================
 * SETUP MENU SCREENS
 * ============================================================================ */

static void scr_copy(void)
{
    static __xdata uint8_t src, dst, f, k, i;
    
    src = cur_preset;
    dst = cur_preset;
    f = 0;
    
    lcd_string_at(0, 0, "COPY                 ");
    lcd_string_at(1, 0, "TO PRESET           ");
    
    for (;;) {
        lcd_goto(0, 5);
        lcd_num(src);
        lcd_goto(0, 8);
        for (i = 0; i < 12; i++) {
            lcd_putc(ee_read(get_preset_addr(src) + REC_NAME + i));
        }
        lcd_goto(1, 10);
        lcd_num(dst);
        lcd_puts("    DOIT");
        
        if (f == 0) lcd_goto(0, 5);
        else lcd_goto(1, f == 1 ? 10 : 16);
        
        k = get_key_pressed();
        if (k == KEY_MODE) return;
        if (k == KEY_SELECT) {
            f = (f == 2) ? 0 : f + 1;
            continue;
        }
        
        if (f == 0) {
            src = wrap_step(src, k, 1, NUM_PRESETS);
        } else if (f == 1) {
            dst = wrap_step(dst, k, 1, NUM_PRESETS);
        } else {
            lcd_string_at(1, 16, "WAIT");
            for (i = 0; i < PRESET_LEN; i++) {
                ed[i] = ee_read(get_preset_addr(src) + i);
            }
            ee_write_data(get_preset_addr(dst), ed, PRESET_LEN);
        }
    }
}

static __bit ch_used(uint8_t val, uint8_t slot)
{
    uint8_t i;
    if (val == 0xFF) return 0;
    
    for (i = 0; i < 16; i++) {
        if (slot != 16 + i && ee_read(CFG_OMC + i) == val) return 1;
    }
    if (slot != 32 && ee_read(CFG_SMC) == val) return 1;
    return 0;
}

static __bit fx_ok(uint8_t f, uint8_t slot)
{
    if (f == 0) return 1;
    if (f == 1) return slot >= 16;
    return slot < 32;
}

static void scr_fx(void)
{
    static __xdata uint8_t slot, f, k, i, v, idx, nv, t;
    
    slot = 0;
    f = 0;
    
    lcd_string_at(0, 0, "FXCONFIG             ");
    lcd_string_at(1, 0, "                    ");
    
    for (;;) {
        v = (slot < 32) ? ee_read(CFG_OMC + slot - 16) : ee_read(CFG_SMC);
        
        lcd_goto(0, 9);
        if (slot < 16) {
            lcd_puts("IN  ");
            lcd_num(slot + 1);
        } else if (slot < 32) {
            lcd_puts("OUT ");
            lcd_num(slot - 15);
        } else {
            lcd_puts("SBLADE");
        }
        lcd_putc(' ');
        
        if (slot < 16) {
            lcd_puts("    ");
        } else if (v & 0x80) {
            lcd_puts("NOCH");
        } else {
            lcd_puts("CH");
            lcd_num(v + 1);
        }
        
        lcd_goto(1, 0);
        if (slot < 32) {
            for (i = 0; i < 8; i++) {
                lcd_putc(ee_read(NAMES_BASE + (uint16_t)slot * 8 + i));
            }
        } else {
            lcd_puts("        ");
        }
        
        if (f == 0) lcd_goto(0, 9);
        else if (f == 1) lcd_goto(0, 19);
        else lcd_goto(1, f - 2);
        
        k = get_key_pressed();
        if (k == KEY_MODE) return;
        
        if (k == KEY_SELECT) {
            do {
                f = (f == 9) ? 0 : f + 1;
            } while (!fx_ok(f, slot));
        } else if (f == 0) {
            slot = wrap_step(slot, k, 0, 32);
        } else if (f == 1) {
            idx = (v & 0x80) ? 0 : v + 1;
            for (t = 0; t < 17; t++) {
                idx = (k == KEY_UP) ? (idx == 16 ? 0 : idx + 1)
                                      : (idx == 0 ? 16 : idx - 1);
                nv = idx ? idx - 1 : 0xFF;
                if (slot == 32 && nv == 0xFF) continue;
                if (!ch_used(nv, slot)) break;
            }
            if (t < 17) {
                ee_write(slot == 32 ? CFG_SMC : CFG_OMC + slot - 16, nv);
            }
        } else {
            ee_write(NAMES_BASE + (uint16_t)slot * 8 + (f - 2),
                          char_step(ee_read(NAMES_BASE + (uint16_t)slot * 8 + (f - 2)), k));
        }
    }
}

/* ============================================================================
 * BACKUP/RESTORE VIA MIDI SYSEX
 * ============================================================================ */

static void backup_save(void)
{
    static __xdata uint16_t off;
    static __xdata uint8_t blk, g, j, m, x, d, buf[7];
    
    off = 0;
    blk = 0;
    ES = 0;
    TI = 1;
    
    for (;;) {
        midi_tx(0xF0);
        midi_tx(0x0C);
        midi_tx(0x00);
        midi_tx(blk);
        
        for (g = 0; g < BLK_GROUPS; g++) {
            m = 0;
            x = 0;
            for (j = 0; j < 7; j++) {
                d = ee_read(off + j);
                m |= (uint8_t)((d >> 7) << j);
                buf[j] = d & 0x7F;
                x ^= buf[j];
            }
            midi_tx(m);
            for (j = 0; j < 7; j++) midi_tx(buf[j]);
            midi_tx(x ^ m);
            off += 7;
            if (off == BACKUP_END) break;
        }
        midi_tx(0xF7);
        if (off == BACKUP_END) break;
        blk++;
        delay_ms(100);
    }
    
    {
        uint8_t t = 90;
        while (!TI && --t);
        TI = 0;
    }
    ES = 1;
}

static uint8_t rx_byte(uint8_t *p)
{
    for (;;) {
        if (RI) {
            RI = 0;
            *p = SBUF;
            if (*p >= 0xF8) continue;
            return 0;
        }
        if (!P1_6) return 1;
    }
}

#define RX(v) do { if (rx_byte(&(v))) return 0xFF; } while (0)

static uint8_t backup_receive(__bit restore)
{
    static __xdata uint16_t off;
    static __xdata uint8_t blk, n, j, m, x, c, d, bad;
    
    off = 0;
    blk = 0;
    RI = 0;
    
    for (;;) {
        RX(c); if (c != 0xF0) return 1;
        RX(c); if (c != 0x0C) return 2;
        RX(c); if (c != 0x00) return 2;
        RX(c); if (c != blk)  return 3;
        
        n = 0;
        do {
            RX(m);
            x = m;
            bad = m;
            for (j = 0; j < 7; j++) {
                RX(d);
                x ^= d;
                bad |= d;
                blkbuf[n + j] = d | (uint8_t)(((m >> j) & 1) << 7);
            }
            RX(c);
            if ((bad & 0x80) || c != x) return 6;
            n += 7;
        } while (n < BLK_GROUPS * 7 && (uint16_t)(off + n) != BACKUP_END);
        
        RX(c);
        if (c != 0xF7) return 4;
        
        if (restore) {
            ee_write_data(off, blkbuf, n);
        } else {
            for (j = 0; j < n; j++) {
                if (ee_read(off + j) != blkbuf[j]) return 5;
            }
        }
        
        off += n;
        if (off == BACKUP_END) return 0;
        blk++;
    }
}

static void backup_run(uint8_t what)
{
    uint8_t r = 0;
    
    if (what == 0) {
        lcd_string_at(0, 0, "SAVE IN PROGRESS....");
        backup_save();
        return;
    }
    
    lcd_string_at(0, 0, what == 2 ? "LOAD IN PROGRESS...." : "VERIFY IN PROGRESS..");
    ES = 0;
    r = backup_receive(what == 2);
    RI = 0;
    ES = 1;
    
    if (r && r != 0xFF) {
        lcd_string_at(0, 0, "RECEIVE ERROR!      ");
        lcd_goto(0, 18);
        lcd_putc('0' + r);
        while (P1_5);
        while (!P1_5);
    }
}

static void scr_backup(void)
{
    static const uint8_t col[3] = { 0, 5, 13 };
    uint8_t f = 0, k;
    
    for (;;) {
        lcd_string_at(0, 0, "BACKUP FUNCTIONS    ");
        lcd_string_at(1, 0, "SAVE VERIFY  RESTORE");
        
        for (;;) {
            lcd_goto(1, col[f]);
            k = get_key_pressed();
            if (k == KEY_MODE) return;
            if (k == KEY_SELECT) {
                f = (f == 2) ? 0 : f + 1;
            } else if (k == KEY_UP) {
                break;
            }
        }
        backup_run(f);
    }
}

static void scr_ctr(void)
{
    static __xdata uint8_t which, f, v, k;
    
    which = 0;
    f = 0;
    
    lcd_string_at(0, 0, "CONTROLLER -->      ");
    lcd_string_at(1, 0, "CONTROL CHANGE #    ");
    
    for (;;) {
        v = ee_read(which ? CFG_CTR1 : CFG_CTR0);
        
        lcd_goto(0, 19);
        lcd_putc('0' + which);
        lcd_goto(1, 17);
        
        if (v & 0x80) lcd_puts("OFF");
        else lcd_num(v);
        
        lcd_goto(f, 19);
        
        k = get_key_pressed();
        if (k == KEY_MODE) return;
        if (k == KEY_SELECT) {
            f ^= 1;
        } else if (f == 0) {
            which ^= 1;
        } else {
            if (k == KEY_UP) {
                v = (v & 0x80) ? 0 : (v == 127 ? (which ? 0x80 : 0) : v + 1);
            } else {
                v = (v & 0x80) ? 127 : (v == 0 ? (which ? 0x80 : 127) : v - 1);
            }
            ee_write(which ? CFG_CTR1 : CFG_CTR0, v);
        }
    }
}

static void scr_pad(void)
{
    uint8_t on = ee_read(CFG_PAD) ? 1 : 0, k;
    
    lcd_string_at(0, 0, "INPUT 1 PREAMP->    ");
    lcd_string_at(1, 0, "                    ");
    
    for (;;) {
        lcd_goto(0, 17);
        lcd_puts(on ? " ON" : "OFF");
        lcd_goto(0, 17);
        
        k = get_key_pressed();
        if (k == KEY_MODE) return;
        if (k == KEY_SELECT) continue;
        
        on ^= 1;
        pad_bit = on ? 0x20 : 0;
        XB(SBSR) = pad_bit;
        ee_write(CFG_PAD, on);
    }
}

static void scr_pst(void)
{
    uint8_t v = ee_read(CFG_PST), k;
    
    if (v > 10) v = 0;
    
    lcd_string_at(0, 0, "PRESET SWITCH TIME  ");
    lcd_string_at(1, 0, "                  MS");
    
    for (;;) {
        lcd_goto(1, 14);
        if (v == 10)     lcd_puts("1000");
        else if (v == 0) lcd_puts("   0");
        else {
            lcd_putc(' ');
            lcd_putc('0' + v);
            lcd_puts("00");
        }
        lcd_goto(1, 17);
        
        k = get_key_pressed();
        if (k == KEY_MODE) break;
        if (k != KEY_SELECT) {
            v = wrap_step(v, k, 0, 10);
        }
    }
    ee_write(CFG_PST, v);
}

static void menu_setup(void)
{
    scr_copy();
    scr_fx();
    scr_backup();
    ee_write(CFG_RC, edit_bits(
        "CONTROL  1  2  3  4 ",
        "TYPES    5  6  7  8 ",
        ee_read(CFG_RC), 'P', 'S', 0
    ));
    scr_ctr();
    scr_pad();
    scr_pst();
}

/* ============================================================================
 * PRESET BANK MANAGER
 * ============================================================================ */

static uint8_t bank_ent(uint8_t bank, uint8_t item)
{
    return ee_read(CFG_BANKS + (uint16_t)bank * 10 + item);
}

static void scr_bank(void)
{
    static __xdata uint8_t mode, nb, bank, item, f, k, v, i;
    static const uint8_t col[3] = { 6, 14, 19 };
    
    mode = ee_read(CFG_CC_ON) & 1;
    nb = ee_read(CFG_BNK_N);
    bank = 0;
    item = 0;
    f = 0;
    
    lcd_string_at(0, 0, "PR MNGR             ");
    
    for (;;) {
        v = bank_ent(bank, item);
        
        if (!mode) {
            lcd_string_at(0, 8, "            ");
            lcd_string_at(1, 0, "    OFF             ");
            lcd_goto(1, 6);
        } else {
            lcd_string_at(1, 0, "BANK    ITEM    PRST");
            lcd_goto(0, 8);
            
            if (!v) {
                lcd_puts("------------");
            } else {
                for (i = 0; i < 12; i++) {
                    lcd_putc(ee_read(get_preset_addr(v) + REC_NAME + i));
                }
            }
            
            lcd_goto(1, 4);
            lcd_putc(bank >= nb ? '*' : ' ');
            lcd_goto(1, 5);
            lcd_num(bank + 1);
            lcd_goto(1, 12);
            lcd_putc((!v || item == 9 || !bank_ent(bank, item + 1)) ? '*' : ' ');
            lcd_goto(1, 13);
            lcd_num(item + 1);
            lcd_goto(1, 18);
            lcd_num(v);
            lcd_goto(1, col[f]);
        }
        
        k = get_key_pressed();
        if (k == KEY_MODE) break;
        
        if (!mode) {
            if (k == KEY_UP) {
                mode = 1;
                bank = 0;
                item = 0;
                f = 0;
            }
            continue;
        }
        
        if (k == KEY_SELECT) {
            f = (f == 2) ? 0 : f + 1;
            continue;
        }
        
        if (f == 0) {
            if (k == KEY_DOWN) {
                if (bank == 0) mode = 0;
                else { bank--; item = 0; }
            } else if (bank < MAX_BANKS - 1 && bank <= nb) {
                bank++;
                item = 0;
            }
        } else if (f == 1) {
            if (k == KEY_DOWN) {
                if (item) item--;
            } else if (v && item < 9) {
                item++;
            }
        } else if (k == KEY_UP) {
            if (v < NUM_PRESETS) {
                ee_write(CFG_BANKS + (uint16_t)bank * 10 + item, v + 1);
                if (bank == nb + 1 && item == 0 && v == 0) {
                    nb++;
                    ee_write(CFG_BNK_N, nb);
                }
            }
        } else if (v > 1) {
            ee_write(CFG_BANKS + (uint16_t)bank * 10 + item, v - 1);
        } else if (v == 1 && (item == 9 || !bank_ent(bank, item + 1))) {
            if (item != 0) {
                ee_write(CFG_BANKS + (uint16_t)bank * 10 + item, 0);
            } else if (bank == nb && bank != 0) {
                ee_write(CFG_BANKS + (uint16_t)bank * 10, 0);
                nb--;
                ee_write(CFG_BNK_N, nb);
            }
        }
    }
    ee_write(CFG_CC_ON, mode);
}

/* ============================================================================
 * PROGRAMMING MENU SYSTEM
 * ============================================================================ */

static void programming_menu(void)
{
    uint8_t sel = 0, k;

    lcd_cmd(0x0F);  /* Blinking cursor */
    lcd_string_at(0, 0, "PROGRAMMING SELECT  ");
    lcd_string_at(1, 0, "PRSET SETUP PRSTMNGR");

    for (;;) {
        lcd_goto(1, sel == 0 ? 0 : (sel == 1 ? 6 : 12));
        k = get_key_pressed();

        if (k == KEY_SELECT) {
            sel = (sel == 2) ? 0 : sel + 1;
        }
        else if (k == KEY_MODE) {
            break;
        }
    }

    if (sel == 0)      menu_preset();
    else if (sel == 1) menu_setup();
    else               scr_bank();

    lcd_cmd(0x0C);
}

/* ============================================================================
 * REMOTE MODE - MIDI note/CC/SysEx driven patching
 * ============================================================================ */

static __xdata uint8_t  rem_st, rem_tmp;
static __xdata uint16_t rem_mask;     /**< Inverter select mask (display only, as in the original) */

static void rem_commit(void)
{
    uint8_t i;
    uint32_t chg = 0;
    
    for (i = 0; i < 32; i++) {
        if (cur_att[i] != 0 && (REM_ATT[i] == 0 || REM_RT[i] != cur_route[i]))
            chg |= 1ul << i;
        tgt_dac[i] = 0;
        if (REM_ATT[i] && !(chg & (1ul << i)))
            tgt_dac[i] = REM_CC[i] < 32 ? REM_VAL[REM_CC[i]] : 0xFF;
    }
    ramp_dacs(pst_time ? (uint8_t)(pst_time * 100u / 32u) : 0);
    
    for (i = 0; i < 32; i++) {
        uint8_t att = REM_ATT[i], rt = REM_RT[i];
        uint8_t c = (chg & (1ul << i)) != 0;
        
        if (c) link_switch(i, cur_route[i], 0);
        if (att != 0 && (cur_att[i] == 0 || c)) link_switch(i, rt, 1);
        cur_att[i] = att;
        cur_route[i] = rt;
        tgt_dac[i] = att ? (REM_CC[i] < 32 ? REM_VAL[REM_CC[i]] : 0xFF) : 0;
    }
    ramp_dacs(pst_time ? (uint8_t)(pst_time * 100u / 32u) : 0);
}

/* SysEx load: rebuild every link from the received records and apply */
static void rem_sysex_apply(void)
{
    uint8_t i;
    
    clear_matrices_and_dacs();
    for (i = 0; i < 32; i++) REM_ATT[i] = 0;
    for (i = 0; i < sx_cnt; i++) {
        REM_ATT[i] = 0xFF;
        REM_RT[i]  = SX_BUF[i * 2];
        REM_CC[i]  = SX_BUF[i * 2 + 1];
    }
    rem_commit();
}

static void rem_show(uint8_t v)
{
    lcd_goto(1, 18);
    lcd_num(v);
}

/* Returns 1 when remote mode must be left */
static __bit rem_note_handle(uint8_t n)
{
    uint8_t v = (n >= 0x30 && n < 0x50) ? n - 0x30 : 0xFF;
    uint8_t i, f;
    
    if (rem_st == 9 && n >= 0x30) {
        rem_st = 0;
        if ((uint8_t)(n - 0x2F) > NUM_PRESETS) return 0;
        rem_on = 0;
        recall_preset(n - 0x2F);
        lcd_status();
        return 1;
    }
    
    if (n < 0x30) {
        switch (n) {
            case 0x24:
                for (i = 0; i < 32; i++) REM_ATT[i] = 0;
                rem_st = 1; lcd_string_at(1, 0, "CLEAR PENDING       "); break;
            case 0x26: rem_st = 2; lcd_string_at(1, 0, "INVERTERS ON        "); break;
            case 0x28: rem_st = 3; lcd_string_at(1, 0, "INVERTERS OFF       "); break;
            case 0x29: rem_st = 6; lcd_string_at(1, 0, "ADD PATCH           "); break;
            case 0x2B: rem_st = 4; lcd_string_at(1, 0, "DEL PATCH           "); break;
            case 0x2D:
                rem_st = 0; rem_commit();
                lcd_string_at(1, 0, "CHANGES EXECUTED    "); break;
            case 0x2F: rem_st = 9; lcd_string_at(1, 0, "EXIT PENDING        "); break;
        }
        return 0;
    }
    if (v == 0xFF) return 0;
    
    switch (rem_st) {
        case 2: case 3:
            if (v < 16) {
                if (rem_st == 2) rem_mask |= 1u << v;
                else             rem_mask &= ~(1u << v);
                rem_show(v + 1);
            }
            break;
        case 4: case 6:
            if (v < 16) { rem_tmp = v; rem_st++; rem_show(v + 1); }
            break;
        case 5: case 7:
            if (v < 16) {
                rem_tmp |= v << 4;
                rem_show(v + 1);
                if (rem_st == 7) rem_st = 8;
                else {
                    rem_st = 0;
                    for (i = 0; i < 32; i++)
                        if (REM_ATT[i] && REM_RT[i] == rem_tmp) { REM_ATT[i] = 0; break; }
                }
            }
            break;
        case 8:
            rem_st = 0;
            rem_show(v);
            f = 0xFF;
            for (i = 0; i < 32; i++)
                if (REM_ATT[i] && REM_RT[i] == rem_tmp) { f = i; break; }
            if (f == 0xFF)
                for (i = 0; i < 32; i++)
                    if (!REM_ATT[i]) { f = i; break; }
            if (f != 0xFF) { REM_ATT[f] = 0xFF; REM_RT[f] = rem_tmp; REM_CC[f] = v; }
            break;
    }
    return 0;
}

static void remote_mode(void)
{
    uint8_t i, v;
    
    TR0 = 0; ET0 = 0;
    sweep_mode = 0;
    rem_st = 0; rem_mask = 0;
    for (i = 0; i < 32; i++) {
        REM_ATT[i] = cur_att[i];
        REM_RT[i]  = cur_route[i];
        REM_CC[i]  = 0xFF;
        REM_VAL[i] = 0xFF;
    }
    lcd_string_at(0, 0, "REMOTE MODE         ");
    lcd_string_at(1, 0, "                    ");
    
    for (;;) {
        if (rem_cc_ev) {
            rem_cc_ev = 0;
            v = rem_ccn;
            if (v < 32) {
                uint8_t d = rem_ccv << 1;
                REM_VAL[v] = d;
                for (i = 0; i < 32; i++) {
                    if (REM_CC[i] != v) continue;
                    mon_wait_edge();
                    dac_write(i, d, 0);
                    cur_dac[i] = d;
                }
            }
        }
        if (sx_end) {
            sx_end = 0;
            rem_sysex_apply();
            lcd_string_at(1, 0, "CHANGES EXECUTED    ");
        }
        if (rem_note_ev) {
            rem_note_ev = 0;
            if (rem_note_handle(rem_note)) return;
        }
    }
}

static void run_programming(void)
{
    XB(LED_PRG) = 0;
    in_play = 0;
    TR0 = 0; ET0 = 0;
    programming_menu();
    XB(LED_PRG) = 0xFF;
    load_config();
    pc_pending = 0;
    recall_preset(cur_preset);
    lcd_status();
    sx_stage = 0; rem_on = 0; rem_note_ev = 0; rem_cc_ev = 0; sx_end = 0;
    in_play = 1;
}

/* ============================================================================
 * MAIN FUNCTION
 * ============================================================================ */

void main(void)
{
    uint8_t b, held = 0, repeat = 0;

    P1 = 0xFF;
    XB(LED_PLAY) = LED_ON;

    clear_matrices_and_dacs();
    lcd_init();
  
    /* Check for special boot modes - SELF TEST */
    if (read_buttons() & (KEY_MODE | KEY_DOWN))
        self_test();/*Never returns*/

    load_config();

    /* Display startup message */
    lcd_goto(0, 0);
    lcd_puts("SWITCHBLADE 16      ");
    lcd_goto(1, 0);
    lcd_puts("FW 3.0.0            ");
    delay_ms(600);

    /* Initialize state */
    pos0 = 0;
    pos1 = 0;
    sweep_mode = 0;
    P1_0 = 0;

    peripherals_init();
    XB(LED_PLAY) = 0;
    recall_preset(1);
    lcd_status();
    in_play = 1;

    /* Main loop */
    for (;;) {
        relays_check_pulse();

        if (rem_on) {
            remote_mode();
            held = 0;
            repeat = 0;
            continue;
        }

        /* Check for programming mode entry */
        if (read_buttons() & KEY_MODE) {
            delay_ms(20);
            if (read_buttons() & KEY_MODE) {
                run_programming();
                held = 0;
                repeat = 0;
                continue;
            }
        }

        /* Process pending MIDI program change */
        if (pc_pending) {
            pc_pending = 0;
            if (pc_value < NUM_PRESETS) {
                recall_preset(pc_value + 1);
                lcd_status();
            }
        }

        /* Update continuous controllers */
        if (pos_dirty) {
            pos_dirty = 0;
            update_continuous();
            lcd_status();
        }

        /* Handle preset navigation buttons */
        b = read_buttons() & (KEY_UP | KEY_DOWN);

        if (b == 0) {
            held = 0;
            repeat = 0;
            continue;
        }

        if (b == (KEY_UP | KEY_DOWN)) continue;

        if (!held) {
            held = 1;
            delay_ms(20);
            if ((read_buttons() & (KEY_UP | KEY_DOWN)) != b) continue;

            change_preset_by(b == KEY_UP ? 1 : -1);
            delay_ms(250);
        }
        else if (++repeat > 4) {
            change_preset_by(b == KEY_UP ? 1 : -1);
            delay_ms(120);
        }
    }
}
