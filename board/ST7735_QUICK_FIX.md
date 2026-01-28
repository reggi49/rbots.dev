# ST7735 Display - Quick Debug Checklist

## BOARD CONFIG
```
SCK:  GPIO8    (SCLK)
MOSI: GPIO9    (SDA/MOSI)
DC:   GPIO10   (Data/Command)
CS:   GPIO7    (Chip Select)
RST:  GPIO21   (Reset - now logged with debug messages)
BL:   ??? (NOT FOUND - CHECK MODULE PIN)
```

## STEP 1: BACKLIGHT TEST (Most Common Issue) ⚡
```
1. Find BL/LED/LITE pin on your TFT module
2. Multimeter: measure voltage BL→GND
   - If 3.3V: ✓ Backlight working
   - If 0V:   ✗ Not connected, must connect to +3.3V
   - If floating: Check power supply

3. Connect module BL pin directly to +3.3V if not connected
   (Optional: through 220Ω resistor for current limiting)

4. Power on ESP32, look at screen
   - Uniformly bright/lit → BACKLIGHT OK, proceed to color test
   - Still black → Power issue, check VCC 3.3V stable
```

## STEP 2: ENABLE & RUN COLOR TEST 🎨
```c
// In board.c around line 3511:
// UNCOMMENT this line:
test_color_fill();

// Then rebuild & flash
idf.py build
idf.py flash monitor
```

**Expected Output in Serial**:
```
I (TIME) FEATURE: TFT: RST pulling LOW
I (TIME) FEATURE: TFT: RST pulling HIGH
I (TIME) FEATURE: TFT: RST sequence complete
...
I (TIME) FEATURE: === ST7735 Color Test Start ===
I (TIME) FEATURE: Test 1: WHITE fill
```

**Expected Display**:
- Test 1: Entire screen WHITE/bright
- Test 2: Entire screen RED
- Test 3: Entire screen GREEN
- Test 4: Entire screen BLUE
- Test 5: Entire screen BLACK
- Test 6: Checkerboard pattern (8×8 black/white squares)
- Test 7: Four colored corners (red, green, blue, white)

## STEP 3: Interpret Results ✅/❌

| Result | Meaning | Fix |
|--------|---------|-----|
| All colors show | DISPLAY WORKS! | Remove `test_color_fill()` comment, normal operation |
| WHITE only, no colors | Address window issue | Check 0x2A/0x2B commands in tft_init() |
| Patterns show but shifted | Coordinate offset issue | Try different MADCTL value (0x00→0x60→0xC0→0xA0) |
| Still black/dark | Backlight not actually on OR reset failing | Check Step 1 + verify RST logs |
| Garbled/colored noise | SPI timing or init sequence wrong | Try 10MHz clock instead of 26MHz |
| Flickering | Power supply too weak | Add 10µF capacitor VCC-GND |

## STEP 4: IF STILL DARK - Systematic Verification

### 4a: Reset Pin Check
```
- Look for 3 log messages in serial output:
  ✓ "TFT: RST pulling LOW"
  ✓ "TFT: RST pulling HIGH"
  ✓ "TFT: RST sequence complete"

- If all 3 appear: Reset toggling works
- If missing: GPIO21 not configured or short to GND
```

### 4b: Power Measurement
```
Multimeter DC Volts:
- TFT VCC pin:  Should be 3.3V ±0.1V
- TFT GND pin:  Should be 0V (common ground)
- TFT BL pin:   Should be 3.3V if connected
- ESP3.3V rail: Should be 3.3V ±0.1V

If any unstable:
- Loose wire? Reseat connectors
- Supply current insufficient? (backlight draws 50-100mA)
- Add 10µF capacitor near TFT VCC
```

### 4c: Pin Swap Check (CRITICAL!)
```
If display still dark after:
- Backlight verified on
- Power stable
- Reset logs appearing
- Color test runs but screen black

THEN try SWAPPING MOSI/SCLK in code:

// Find in tft_init() around line 3115:
spi_bus_config_t buscfg = {
    .miso_io_num = -1,
    .mosi_io_num = TFT_MOSI,    // ← SWAP with next line
    .sclk_io_num = TFT_SCK,     // ← SWAP with previous line
    ...
};

Change to:
.mosi_io_num = TFT_SCK,
.sclk_io_num = TFT_MOSI,

Then rebuild/flash
```

### 4d: Clock Speed Reduction
```
If display shows GARBLED pattern or noise:

// Find in tft_init() around line 3117:
.clock_speed_hz = 26000000,

Change to:
.clock_speed_hz = 10000000,  // Try slower clock

Rebuild/flash
```

## STEP 5: Once Display Works ✅

After color test passes:
1. **Remove test function call** (comment it back out)
2. **Verify normal startup** (Mic indicator ready)
3. **Ready for mic level animation feature**

---

## Files Modified
- `board/main/board.c`:
  - Line ~3130: Added RST debug logging
  - Line ~1607: Added `test_color_fill()` function
  - Line ~3514: Commented-out test call in app_main()

- `board/ST7735_DIAGNOSTICS.md`: Full diagnostic guide (this file)

---

## Color Codes in RGB565
```
0xFFFF = White   (R=31, G=63, B=31)
0xF800 = Red     (R=31, G=0,  B=0)
0x07E0 = Green   (R=0,  G=63, B=0)
0x001F = Blue    (R=0,  G=0,  B=31)
0x0000 = Black   (R=0,  G=0,  B=0)
```

---

## Contact Points for Further Help
- If still black after all checks: Check ST7735 variant (R/S/BLACK variants need different init)
- If garbled: Try ST7735R init sequence instead of ST7735 standard
- If shifted: Try MADCTL rotation values (0x60, 0xC0, 0xA0)
