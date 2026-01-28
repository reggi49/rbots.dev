# ST7735 Display Diagnostics - ESP32-C3 RBOT

## Current Pin Mapping
```
TFT_SCK   = GPIO8    (SCLK)
TFT_MOSI  = GPIO9    (MOSI/SDA)
TFT_DC    = GPIO10   (Data/Command select)
TFT_CS    = GPIO7    (Chip Select)
TFT_RST   = GPIO21   (Reset - ACTIVE HIGH after init)
TFT_BL    = ???      (Backlight - CHECK MODULE)
```

---

## Most Likely Causes (Ordered by Probability)

### 🔴 **#1: BACKLIGHT NOT POWERED (75% probability if no display visible)**
**Symptom**: Screen completely black, no response to color fills

**Quick Check**:
```bash
# On the module itself, find pin labeled: BL, LED, LITE, or similar
# Measure voltage with multimeter:
# - Between BL and GND: should be 3.3V (or at least 1.8V+)
# - If 0V or floating: BACKLIGHT IS OFF
```

**Fix**: Connect backlight to 3.3V directly (or via 220Ω resistor if current limiting needed)
```
TFT Module BL pin ──[220Ω]── +3.3V
                   └─ GND (optional path)
```

---

### 🟡 **#2: RESET PIN NOT HELD CORRECTLY**
**Symptom**: Display garbled, partial init, inconsistent behavior

**Current Code** (line 3135-3137 in board.c):
```c
gpio_set_level(TFT_RST, 0);  // Pull LOW 20ms
vTaskDelay(pdMS_TO_TICKS(20));
gpio_set_level(TFT_RST, 1);  // Pull HIGH 120ms
vTaskDelay(pdMS_TO_TICKS(120));
```

**Quick Check**:
- GPIO21 configured as OUTPUT? ✓ (line 3095 pin_bit_mask)
- RST going LOW then HIGH? (add Serial log to verify)
- Capacitor on RST line? (help stabilize)

**If RST stuck LOW**: Check for short to GND on GPIO21

---

### 🟠 **#3: SPI CONTROL ISSUES**
**Symptom**: Screen still black even with backlight on; no response to commands

**Check List**:
- [ ] **CS (GPIO7)**: Should be LOW during SPI transaction, HIGH when idle
  ```c
  // ESP-IDF SPI should handle this automatically
  // If manual: verify spi_device_transmit() pulls CS
  ```
  
- [ ] **DC (GPIO10)**: 
  - DC=0 for commands (like 0x01, 0x11, 0x29)
  - DC=1 for data (pixel data, arguments)
  - **Current code** (line 1545): ✓ Correct logic
  
- [ ] **SCLK/MOSI timing**: Frequency set to 26 MHz (line 3116 clock_speed_hz)
  - Try reducing to 10 MHz if seeing garbled data
  - Some cheap modules need slower SPI

- [ ] **Pin Swap**: Double-check in hardware:
  - MOSI physically connected to GPIO9
  - SCK physically connected to GPIO8
  - Not reversed

---

### 🟢 **#4: INIT SEQUENCE INCOMPLETE**
**Symptom**: Backlight ON + power stable, but display shows color patterns incorrectly

**Current Init** (lines 3141-3162):
```
0x01  → Software reset
0x11  → Sleep out
0x36  → Memory access control (rotation)
0x3A  → Color mode (0x05 = 16-bit RGB565)
0x2A  → Column address set
0x2B  → Row address set
0x29  → Display ON
```

**Issue**: ST7735 variants (RED/GREEN/BLACK tab) need different init sequences
- **For ST7735S (most common)**: May need additional commands
  - 0xB1 (Frame rate control)
  - 0xB4 (Display inversion)
  - 0xC5 (VCOMH voltage)
  
**Quick Test**:
- Try fill screen WHITE → can you see white rectangle?
- If YES: Init OK, issue is elsewhere
- If NO: Init likely wrong or power insufficient

---

### 🔵 **#5: DISPLAY COORDINATE/OFFSET MISMATCH**
**Symptom**: Colors appear, but wrong areas light up; image shifted

**Check**:
- Screen size set to 128x160? (lines 1567-1568 use 0x7F, 0x9F max)
- Rotation (MADCTL = 0x00) correct for your physical layout?
- Try MADCTL values:
  - 0x00 → Portrait 0°
  - 0x60 → Landscape 90°
  - 0xC0 → Portrait 180°
  - 0xA0 → Landscape 270°

---

### ⚫ **#6: POWER SUPPLY INSUFFICIENT**
**Symptom**: Backlight flickers, display unstable, color shifts

**Check**:
- [ ] ESP32-C3 3.3V rail: measure with multimeter
- [ ] TFT VCC stable at 3.3V when backlight ON?
- [ ] Backlight draws 20-100mA depending on brightness
- [ ] Total supply can provide 200+ mA? (ESP + TFT + Mic + WiFi)

**Fix**: Add 10µF capacitor across VCC-GND near TFT module

---

## Diagnostic Sequence (Quick Path)

### Step 1: Power Verification (2 min)
```
1. Measure TFT VCC with multimeter: should be 3.3V ±0.1V
2. Measure TFT GND: should be 0V (common ground with ESP)
3. Measure TFT BL pin: should be 3.3V if connected
   → If 0V: Connect BL to 3.3V and test
```

### Step 2: Backlight Test (1 min)
```
1. Disconnect BL from wherever it is
2. Connect directly: BL → 3.3V (use jumper wire)
3. Power on ESP32-C3
4. Look at screen: Is it uniformly lit (backlight on)?
   → YES: Backlight working, init/SPI issue
   → NO: Check power supply (VCC correct?)
```

### Step 3: Reset Pin Verification (2 min)
```
1. Add debug log to tft_init():
   ESP_LOGI(TAG, "RST: pulling LOW");
   gpio_set_level(TFT_RST, 0);
   vTaskDelay(pdMS_TO_TICKS(20));
   ESP_LOGI(TAG, "RST: pulling HIGH");
   gpio_set_level(TFT_RST, 1);
   vTaskDelay(pdMS_TO_TICKS(120));
   ESP_LOGI(TAG, "RST: done");

2. Recompile and check serial output
   → Look for three log messages in order
   → If missing: GPIO21 not configured or pinout issue
```

### Step 4: Color Fill Test (3 min)
```
See TEST_CODE section below
- Add test_color_fill() to app_main()
- Test pattern: WHITE → RED → GREEN → BLUE → BLACK
- Each color displays 2 seconds

Expected:
- If you see white rectangle: SPI + Init working
- If you see colors change: pixel writing working
- If nothing changes: address window not set correctly
```

### Step 5: SPI Pin Swap Check (1 min)
```
1. Manually verify wire connections:
   - MOSI wire to GPIO9 (not GPIO8)
   - SCLK wire to GPIO8 (not GPIO9)
   
2. If reversed: swap in code:
   .mosi_io_num = TFT_SCK,  // SWAP
   .sclk_io_num = TFT_MOSI, // SWAP
   Then rebuild
```

---

## Test Code Snippet

Add this function to board.c (e.g., after tft_fill_screen):

```c
/**
 * Test ST7735 with color fill patterns
 * Call from app_main() during debug
 */
static void test_color_fill(void)
{
    ESP_LOGI(TAG, "=== ST7735 Color Test Start ===");
    
    // Test 1: White (checks backlight + pixel writing)
    ESP_LOGI(TAG, "Test 1: WHITE fill");
    tft_fill_screen(0xFFFF);  // White: R=255 G=255 B=255
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 2: Red (0xFF, 0x00, 0x00 in RGB565)
    ESP_LOGI(TAG, "Test 2: RED fill");
    tft_fill_screen(0xF800);  // Red
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 3: Green
    ESP_LOGI(TAG, "Test 3: GREEN fill");
    tft_fill_screen(0x07E0);  // Green
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 4: Blue
    ESP_LOGI(TAG, "Test 4: BLUE fill");
    tft_fill_screen(0x001F);  // Blue
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 5: Black (checks if display can go dark)
    ESP_LOGI(TAG, "Test 5: BLACK fill");
    tft_fill_screen(0x0000);  // Black
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 6: Checkerboard pattern (address window + DC control)
    ESP_LOGI(TAG, "Test 6: Checkerboard");
    for(int y=0; y<SCREEN_HEIGHT; y+=8) {
        for(int x=0; x<SCREEN_WIDTH; x+=8) {
            uint16_t color = ((x + y)/8) % 2 ? 0xFFFF : 0x0000;
            tft_fill_rect(x, y, 8, 8, color);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "=== Color Test Complete ===");
}
```

**To use**: Add to `app_main()` right after `tft_init()` returns:
```c
if (!tft_init()) {
    ESP_LOGE(TAG, "TFT init failed");
    return;
}
test_color_fill();  // ADD THIS LINE
// ... rest of init ...
```

---

## Tuning Thresholds for Mic Indicator (Once Display Works)

Once you confirm display is working, these thresholds will be used:

```c
// Red: No mic input (RMS < threshold)
#define MIC_LEVEL_RED_MAX    0.2f   // RMS < 20% full scale

// Yellow: Normal speech (RMS in range)
#define MIC_LEVEL_YELLOW_MIN 0.2f
#define MIC_LEVEL_YELLOW_MAX 0.7f

// Green: Strong voice OR wake word accepted
#define MIC_LEVEL_GREEN_MIN  0.7f

// Green also triggered by g_wake_accepted flag (set by wakeword callback)
```

EMA smoothing: alpha = 0.12 (12% new value, 88% history)
```c
g_mic_level_ema = 0.12f * raw_level + 0.88f * g_mic_level_ema;
```

---

## Summary Table

| Issue | Symptom | Check | Fix |
|-------|---------|-------|-----|
| Backlight OFF | Completely black | Measure BL pin | Connect BL→3.3V |
| Reset stuck | Garbled display | GPIO21 output log | Check reset circuit |
| SPI slow/stuck | No response | Try 10MHz clock | Reduce speed |
| Power weak | Flicker/shift | Measure VCC | Add 10µF cap |
| Init sequence | Colors wrong | Test WHITE fill | Check ST7735 variant |
| Pins swapped | Garbled or black | Verify MOSI/SCLK | Swap in code |

---

## Next Steps

1. **Do Step 1-2 first** (5 min, fixes 75% of issues)
2. **If backlight works, do Step 3-4** (5 min)
3. **If color test shows white**, SPI is OK → focus on init sequence
4. **If color test shows colors**, display is fully functional!

Good luck! 🎯
