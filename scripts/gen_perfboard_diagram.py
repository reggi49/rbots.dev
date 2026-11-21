import sys
import os

W, H = 1520, 2400
svg = []
svg.append(f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">
<defs>
  <style>
    .title {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-weight: 800; font-size: 27px; fill: #ffffff; }}
    .subtitle {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-weight: 500; font-size: 13.5px; fill: #94a3b8; }}
    .header-tag {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-weight: 800; font-size: 13px; letter-spacing: 0.5px; }}
    .pin-title {{ font-family: "SF Mono", Monaco, Consolas, monospace; font-size: 12px; font-weight: 700; }}
    .pin-sub {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 10px; font-weight: 600; }}
    .mod-label {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 13px; font-weight: 800; }}
    .comp-pin {{ font-family: "SF Mono", Monaco, Consolas, monospace; font-size: 11px; font-weight: 700; }}
    .legend-title {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 14px; font-weight: 700; fill: #f8fafc; }}
    .legend-text {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 11.5px; fill: #cbd5e1; }}
  </style>
  <filter id="shadow" x="-5%" y="-5%" width="110%" height="115%">
    <feDropShadow dx="0" dy="4" stdDeviation="6" flood-color="#000000" flood-opacity="0.45"/>
  </filter>
</defs>
''')

# Dark background
svg.append(f'<rect width="{W}" height="{H}" fill="#0a0f1d"/>')

# Title Card
svg.append('''
<rect x="50" y="25" width="1420" height="90" rx="14" fill="#1e293b" stroke="#334155" stroke-width="2" filter="url(#shadow)"/>
<text x="80" y="62" class="title">🤖 R-BOT PERFBOARD WIRING &amp; PIN ARCHITECTURE (10×22 GRID)</text>
<text x="80" y="90" class="subtitle">Skema Solder Perfboard Lengkap • MAX98357A Vin, GND, SD, GAIN Detail • SDA ke GPIO 4 &amp; SCK ke GPIO 5 • ESP32-S3 N16R8</text>
''')

# Grid constants
ox = 455
oy = 230
cw = 67
rh = 63

# Perfboard Green Canvas
board_w = 9 * cw + 110
board_h = 21 * rh + 130
svg.append(f'<rect x="{ox - 55}" y="{oy - 80}" width="{board_w}" height="{board_h}" rx="18" fill="#064e3b" stroke="#047857" stroke-width="4" filter="url(#shadow)"/>')

# Column Indicators
for c in range(10):
    hx = ox + c * cw
    c_label = f"C{c+1}"
    if c == 0: c_label = "C1 [L]"
    elif c == 9: c_label = "C10 [R]"
    svg.append(f'<text x="{hx}" y="{oy - 58}" font-family="monospace" font-size="12" font-weight="700" fill="#a7f3d0" text-anchor="middle">{c_label}</text>')

# Row Indicators
for r in range(22):
    hy = oy + r * rh
    svg.append(f'<text x="{ox - 75}" y="{hy + 5}" font-family="monospace" font-size="12" font-weight="700" fill="#6ee7b7" text-anchor="middle">R{r+1}</text>')
    svg.append(f'<text x="{ox + 9*cw + 75}" y="{hy + 5}" font-family="monospace" font-size="12" font-weight="700" fill="#6ee7b7" text-anchor="middle">R{r+1}</text>')

# Draw Copper Donut Holes
for r in range(22):
    for c in range(10):
        hx = ox + c * cw
        hy = oy + r * rh
        svg.append(f'<circle cx="{hx}" cy="{hy}" r="11" fill="#042f2e" stroke="#10b981" stroke-width="1.8"/>')
        svg.append(f'<circle cx="{hx}" cy="{hy}" r="4.5" fill="#090d16" stroke="#047857" stroke-width="0.8"/>')

# ==================== PIN DEFINITIONS ====================
left_pins = [
    ("GND", "Common GND", "#10b981", "#022c22"),
    ("TX", "GPIO 43 (Log)", "#64748b", "#1e293b"),
    ("RX", "GPIO 44 (Log)", "#64748b", "#1e293b"),
    ("GPIO 1", "LCD CS (Chip Select)", "#22c55e", "#052e16"),
    ("GPIO 2", "LCD RESET", "#f97316", "#431407"),
    ("GPIO 42", "LCD A0/DC (Command)", "#eab308", "#422006"),
    ("GPIO 41", "MIC SD (I2S0 Data In)", "#06b6d4", "#083344"),
    ("GPIO 40", "Cadangan / Kosong", "#64748b", "#1e293b"),
    ("GPIO 39", "Cadangan / Kosong", "#64748b", "#1e293b"),
    ("GPIO 38", "Cadangan / Kosong", "#64748b", "#1e293b"),
    ("GPIO 37", "⚠️ PSRAM Octal (JANGAN)", "#ef4444", "#450a0a"),
    ("GPIO 36", "⚠️ PSRAM Octal (JANGAN)", "#ef4444", "#450a0a"),
    ("GPIO 35", "⚠️ PSRAM Octal (JANGAN)", "#ef4444", "#450a0a"),
    ("GPIO 0", "⚠️ Strapping (JANGAN)", "#ef4444", "#450a0a"),
    ("GPIO 45", "⚠️ Strapping (JANGAN)", "#ef4444", "#450a0a"),
    ("GPIO 48", "RGB LED Onboard", "#a855f7", "#3b0764"),
    ("GPIO 47", "Cadangan / Kosong", "#64748b", "#1e293b"),
    ("GPIO 21", "Cadangan / Kosong", "#64748b", "#1e293b"),
    ("GPIO 20", "⚠️ USB D+ (JANGAN)", "#ef4444", "#450a0a"),
    ("GPIO 19", "⚠️ USB D- (JANGAN)", "#ef4444", "#450a0a"),
    ("GND", "Common GND (Bawah)", "#10b981", "#022c22"),
    ("GND", "Common GND (Bawah)", "#10b981", "#022c22"),
]

right_pins = [
    ("3V3", "3.3V LDO Output", "#ec4899", "#500724"),
    ("3V3", "3.3V LDO Output", "#ec4899", "#500724"),
    ("RST", "EN / Chip Reset", "#64748b", "#1e293b"),
    ("GPIO 4", "LCD SDA (SPI MOSI) ✓", "#3b82f6", "#172554"),
    ("GPIO 5", "LCD SCK (SPI Clock) ✓", "#a855f7", "#3b0764"),
    ("GPIO 6", "Cadangan / Kosong", "#64748b", "#1e293b"),
    ("GPIO 7", "Cadangan / Kosong", "#64748b", "#1e293b"),
    ("GPIO 15", "MIC WS (I2S0 LRCLK)", "#eab308", "#422006"),
    ("GPIO 16", "MIC SCK (I2S0 BCLK)", "#a855f7", "#3b0764"),
    ("GPIO 17", "Cadangan / Kosong", "#64748b", "#1e293b"),
    ("GPIO 18", "Cadangan / Kosong", "#64748b", "#1e293b"),
    ("GPIO 8", "SPK LRC (I2S1 LRCLK)", "#eab308", "#422006"),
    ("GPIO 3", "⚠️ Strapping (JANGAN)", "#ef4444", "#450a0a"),
    ("GPIO 46", "⚠️ Strapping (JANGAN)", "#ef4444", "#450a0a"),
    ("GPIO 9", "SPK BCLK (I2S1 BCLK)", "#a855f7", "#3b0764"),
    ("GPIO 10", "SPK DIN (I2S1 Data In)", "#3b82f6", "#172554"),
    ("GPIO 11", "MOT IN1 (Roda L+ PWM)", "#06b6d4", "#083344"),
    ("GPIO 12", "MOT IN2 (Roda L- PWM)", "#0ea5e9", "#082f49"),
    ("GPIO 13", "MOT IN4 (Roda R- PWM)", "#8b5cf6", "#2e1065"),
    ("GPIO 14", "MOT IN3 (Roda R+ PWM)", "#6366f1", "#1e1b4b"),
    ("5Vin", "5.0V Boost (MH-CD42)", "#f43f5e", "#4c0519"),
    ("GND", "Common GND (Bawah)", "#10b981", "#022c22"),
]

# Draw Left Header Badges
for r, (pname, pdesc, col, bg) in enumerate(left_pins):
    hy = oy + r * rh
    bx = 95
    bw = 255
    bh = 46
    by = hy - 23
    svg.append(f'''
    <g>
      <rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="8" fill="{bg}" stroke="{col}" stroke-width="1.6"/>
      <text x="{bx + 16}" y="{by + 20}" class="pin-title" fill="{col}">{pname}</text>
      <text x="{bx + 16}" y="{by + 36}" class="pin-desc" fill="#94a3b8">{pdesc}</text>
      <line x1="{bx + bw}" y1="{hy}" x2="{ox - 15}" y2="{hy}" stroke="{col}" stroke-width="1.8" stroke-dasharray="3,3" opacity="0.7"/>
    </g>
    ''')

# Draw Right Header Badges
for r, (pname, pdesc, col, bg) in enumerate(right_pins):
    hy = oy + r * rh
    bx = ox + 9 * cw + 95
    bw = 255
    bh = 46
    by = hy - 23
    svg.append(f'''
    <g>
      <rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="8" fill="{bg}" stroke="{col}" stroke-width="1.6"/>
      <text x="{bx + 16}" y="{by + 20}" class="pin-title" fill="{col}">{pname}</text>
      <text x="{bx + 16}" y="{by + 36}" class="pin-desc" fill="#94a3b8">{pdesc}</text>
      <line x1="{ox + 9*cw + 15}" y1="{hy}" x2="{bx}" y2="{hy}" stroke="{col}" stroke-width="1.8" stroke-dasharray="3,3" opacity="0.7"/>
    </g>
    ''')

# ==================== MODULE OVERLAYS ====================

# 1. LCD Header at Row 1 (Cols 2 to 9, i.e. c=1 to c=8)
lcd_pins = [
    ("VCC", "#ec4899"),
    ("GND", "#10b981"),
    ("CS", "#22c55e"),
    ("RST", "#f97316"),
    ("A0", "#eab308"),
    ("SDA", "#3b82f6"),
    ("SCK", "#a855f7"),
    ("LED", "#ec4899"),
]
lx1 = ox + 1 * cw - 20
lx2 = ox + 8 * cw + 20
ly = oy - 24
svg.append(f'''
<g filter="url(#shadow)">
  <rect x="{lx1}" y="{ly}" width="{lx2 - lx1}" height="48" rx="8" fill="#881337" stroke="#fb7185" stroke-width="2"/>
  <text x="{(lx1+lx2)/2}" y="{ly - 10}" class="header-tag" fill="#fda4af" text-anchor="middle">🔴 SOKET 8-PIN: LAYAR ST7735 TFT 1.8" (128×160)</text>
</g>
''')
for i, (lp, lcol) in enumerate(lcd_pins):
    hx = ox + (i+1) * cw
    svg.append(f'''
    <circle cx="{hx}" cy="{oy}" r="15" fill="#4c0519" stroke="{lcol}" stroke-width="2"/>
    <text x="{hx}" y="{oy + 4}" class="comp-pin" fill="#ffffff" text-anchor="middle">{lp}</text>
    ''')

# 2. INMP441 Microphone (Round, Rows 7-9, Cols 3-6)
mic_cx = ox + 4.5 * cw
mic_cy = oy + 7.5 * rh
svg.append(f'''
<g filter="url(#shadow)">
  <circle cx="{mic_cx}" cy="{mic_cy}" r="78" fill="#18181b" stroke="#eab308" stroke-width="3"/>
  <circle cx="{mic_cx}" cy="{mic_cy}" r="32" fill="#27272a" stroke="#71717a" stroke-width="1.5"/>
  <text x="{mic_cx}" y="{mic_cy - 46}" class="header-tag" fill="#fef08a" text-anchor="middle">🎙️ INMP441</text>
  <text x="{mic_cx}" y="{mic_cy + 5}" font-family="sans-serif" font-size="11" font-weight="700" fill="#a1a1aa" text-anchor="middle">MEMS MIC</text>
  <text x="{mic_cx}" y="{mic_cy + 46}" font-family="sans-serif" font-size="10" font-weight="700" fill="#eab308" text-anchor="middle">I2S0 (RX)</text>
</g>
''')

# Mic Pins
mic_pins = [
    ("SD", 7, 3, "#06b6d4"),
    ("VDD", 8, 3, "#ec4899"),
    ("GND", 9, 3, "#10b981"),
    ("L/R", 7, 6, "#10b981"),
    ("WS", 8, 6, "#eab308"),
    ("SCK", 9, 6, "#a855f7"),
]
for p_label, pr, pc, p_col in mic_pins:
    px = ox + pc * cw
    py = oy + (pr - 1) * rh
    svg.append(f'''
    <circle cx="{px}" cy="{py}" r="12" fill="#27272a" stroke="{p_col}" stroke-width="2"/>
    <text x="{px}" y="{py + 4}" class="comp-pin" fill="#f4f4f5" text-anchor="middle">{p_label}</text>
    ''')

# 3. MAX98357A Audio Amp (Purple, Rows 11-15, Cols 2-8, Reversed: LRC Left -> Vin Right)
amp_x = ox + 1.6 * cw
amp_y = oy + 10.5 * rh
amp_w = 6.2 * cw
amp_h = 4.8 * rh
svg.append(f'''
<g filter="url(#shadow)">
  <rect x="{amp_x}" y="{amp_y}" width="{amp_w}" height="{amp_h}" rx="14" fill="#3b0764" stroke="#a855f7" stroke-width="2.5"/>
  <text x="{amp_x + amp_w/2}" y="{amp_y + 30}" class="header-tag" fill="#e9d5ff" text-anchor="middle">🔊 MAX98357A (TERBALIK: LRC KIRI ➔ VIN KANAN)</text>
  <rect x="{amp_x + 50}" y="{amp_y + 46}" width="70" height="30" rx="6" fill="#1e1b4b" stroke="#818cf8" stroke-width="1.5"/>
  <text x="{amp_x + 85}" y="{amp_y + 66}" class="comp-pin" fill="#ffffff" text-anchor="middle">SPK -</text>
  <rect x="{amp_x + amp_w - 120}" y="{amp_y + 46}" width="70" height="30" rx="6" fill="#1e1b4b" stroke="#818cf8" stroke-width="1.5"/>
  <text x="{amp_x + amp_w - 85}" y="{amp_y + 66}" class="comp-pin" fill="#ffffff" text-anchor="middle">SPK +</text>
  <text x="{amp_x + amp_w/2}" y="{amp_y + 98}" font-family="sans-serif" font-size="10.5" font-weight="700" fill="#c084fc" text-anchor="middle">⚠️ BTL: SPK- JANGAN ke GND! Langsung ke Speaker</text>
  <text x="{amp_x + amp_w/2}" y="{amp_y + 116}" font-family="sans-serif" font-size="10" font-weight="700" fill="#93c5fd" text-anchor="middle">SD &amp; GAIN: Biarkan KOSONG (Default Aktif 9dB Stereo-Mix)</text>
</g>
''')
amp_pins = [
    ("LRC", 15, 2, "#eab308"),
    ("BCLK", 15, 3, "#a855f7"),
    ("DIN", 15, 4, "#3b82f6"),
    ("GAIN", 15, 5, "#64748b"),
    ("SD", 15, 6, "#64748b"),
    ("GND", 15, 7, "#10b981"),
    ("Vin", 15, 8, "#f43f5e"),
]
for p_label, pr, pc, p_col in amp_pins:
    px = ox + pc * cw
    py = oy + (pr - 1) * rh
    svg.append(f'''
    <circle cx="{px}" cy="{py}" r="13" fill="#2e1065" stroke="{p_col}" stroke-width="2"/>
    <text x="{px}" y="{py + 4}" class="comp-pin" fill="#ffffff" text-anchor="middle">{p_label}</text>
    ''')

# 4. DRV8833 Dual Motor Driver (Black, Rows 17-22, Cols 2-7, Rotated 180°)
drv_x = ox + 1.8 * cw
drv_y = oy + 16.3 * rh
drv_w = 5.4 * cw
drv_h = 5.2 * rh
svg.append(f'''
<g filter="url(#shadow)">
  <rect x="{drv_x}" y="{drv_y}" width="{drv_w}" height="{drv_h}" rx="14" fill="#111827" stroke="#38bdf8" stroke-width="2.5"/>
  <text x="{drv_x + drv_w/2}" y="{drv_y + 32}" class="header-tag" fill="#7dd3fc" text-anchor="middle">⚙️ DRV8833 DUAL H-BRIDGE (DIPUTAR 180°)</text>
  <rect x="{drv_x + 35}" y="{drv_y + drv_h - 45}" width="165" height="28" rx="6" fill="#14532d" stroke="#22c55e" stroke-width="1.5"/>
  <text x="{drv_x + 117}" y="{drv_y + drv_h - 26}" font-family="sans-serif" font-size="10" font-weight="800" fill="#86efac" text-anchor="middle">✓ JUMPER J1 DISOLDER</text>
</g>
''')
drv_left = [
    ("ULT", 17, 2, "#64748b"),
    ("OUT4", 18, 2, "#f59e0b"),
    ("OUT3", 19, 2, "#f59e0b"),
    ("OUT2", 20, 2, "#38bdf8"),
    ("OUT1", 21, 2, "#38bdf8"),
    ("EEP", 22, 2, "#22c55e"),
]
drv_right = [
    ("IN1", 17, 7, "#06b6d4"),
    ("IN2", 18, 7, "#0ea5e9"),
    ("VCC", 19, 7, "#f43f5e"),
    ("GND", 20, 7, "#10b981"),
    ("IN3", 21, 7, "#6366f1"),
    ("IN4", 22, 7, "#8b5cf6"),
]
for p_label, pr, pc, p_col in drv_left + drv_right:
    px = ox + pc * cw
    py = oy + (pr - 1) * rh
    svg.append(f'''
    <circle cx="{px}" cy="{py}" r="12" fill="#0f172a" stroke="{p_col}" stroke-width="2"/>
    <text x="{px}" y="{py + 4}" class="comp-pin" fill="#ffffff" text-anchor="middle">{p_label}</text>
    ''')

# ==================== PHYSICAL WIRE TRACES ====================
def wire(points, color, width=4, dash=None):
    pts = " ".join([f"{p[0]},{p[1]}" for p in points])
    d_attr = f'stroke-dasharray="{dash}"' if dash else ''
    res = f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round" {d_attr}/>'
    for p in points:
        res += f'<circle cx="{p[0]}" cy="{p[1]}" r="{width/2 + 2}" fill="{color}"/>'
    return res

# 1. LCD CS (Col 4, Row 1) -> GPIO 1 (Col 1, Row 4)
svg.append(wire([(ox + 3*cw, oy), (ox + 3*cw, oy + 3*rh), (ox, oy + 3*rh)], "#22c55e", width=4.5))

# 2. LCD RESET (Col 5, Row 1) -> GPIO 2 (Col 1, Row 5)
svg.append(wire([(ox + 4*cw, oy), (ox + 4*cw, oy + 4*rh), (ox, oy + 4*rh)], "#f97316", width=4.5))

# 3. LCD A0/DC (Col 6, Row 1) -> GPIO 42 (Col 1, Row 6)
svg.append(wire([(ox + 5*cw, oy), (ox + 5*cw, oy + 5*rh), (ox, oy + 5*rh)], "#eab308", width=4.5))

# 4. LCD SDA (Col 7, Row 1) -> GPIO 4 (Col 10, Row 4) [BLUE WIRE - SOLDERED TO GPIO 4]
svg.append(wire([(ox + 6*cw, oy), (ox + 6*cw, oy + 3*rh), (ox + 9*cw, oy + 3*rh)], "#3b82f6", width=5.5))

# 5. LCD SCK (Col 8, Row 1) -> GPIO 5 (Col 10, Row 5) [PURPLE WIRE - SOLDERED TO GPIO 5]
svg.append(wire([(ox + 7*cw, oy), (ox + 7*cw, oy + 4*rh), (ox + 9*cw, oy + 4*rh)], "#a855f7", width=5.5))

# 6. LCD LED (Col 9, Row 1) -> 3V3 (Col 10, Row 1) [Bridge 1 lubang!]
svg.append(wire([(ox + 8*cw, oy), (ox + 9*cw, oy)], "#ec4899", width=6))

# 7. LCD GND (Col 3, Row 1) -> GND (Col 1, Row 1) [Bridge 2 lubang!]
svg.append(wire([(ox + 2*cw, oy), (ox, oy)], "#10b981", width=5))

# 8. LCD VCC (Col 2, Row 1) -> 3.3V (Col 10, Row 2)
svg.append(wire([(ox + 1*cw, oy), (ox + 1*cw, oy - 48), (ox + 9*cw, oy - 48), (ox + 9*cw, oy + 1*rh)], "#ec4899", width=4, dash="6,3"))

# 9. MIC SD (Col 3, Row 7) -> GPIO 41 (Col 1, Row 7) [Lurus sebaris!]
svg.append(wire([(ox + 3*cw, oy + 6*rh), (ox, oy + 6*rh)], "#06b6d4", width=5))

# 10. MIC WS (Col 6, Row 8) -> GPIO 15 (Col 10, Row 8) [Lurus sebaris!]
svg.append(wire([(ox + 6*cw, oy + 7*rh), (ox + 9*cw, oy + 7*rh)], "#eab308", width=5))

# 11. MIC SCK (Col 6, Row 9) -> GPIO 16 (Col 10, Row 9) [Lurus sebaris!]
svg.append(wire([(ox + 6*cw, oy + 8*rh), (ox + 9*cw, oy + 8*rh)], "#a855f7", width=5))

# 12. MIC L/R (Col 6, Row 7) -> GND (di Row 9 Col 3 via tepi atas tanpa silang)
svg.append(wire([(ox + 6*cw, oy + 6*rh), (ox + 6*cw, oy + 5.5*rh), (ox + 3*cw, oy + 5.5*rh), (ox + 3*cw, oy + 8*rh)], "#10b981", width=3.5, dash="4,3"))

# 13. MAX98357A LRC (Col 2, Row 15) -> GPIO 8 (Col 10, Row 12) [Via Row 10/12]
svg.append(wire([(ox + 2*cw, oy + 14*rh), (ox + 1.2*cw, oy + 14*rh), (ox + 1.2*cw, oy + 9.5*rh), (ox + 8.3*cw, oy + 9.5*rh), (ox + 8.3*cw, oy + 11*rh), (ox + 9*cw, oy + 11*rh)], "#eab308", width=4.5))

# 14. MAX98357A BCLK (Col 3, Row 15) -> GPIO 9 (Col 10, Row 15) [Via Row 15.4]
svg.append(wire([(ox + 3*cw, oy + 14*rh), (ox + 3*cw, oy + 14.55*rh), (ox + 8.5*cw, oy + 14.55*rh), (ox + 8.5*cw, oy + 14*rh), (ox + 9*cw, oy + 14*rh)], "#a855f7", width=4.5))

# 15. MAX98357A DIN (Col 4, Row 15) -> GPIO 10 (Col 10, Row 16) [Drop 1 baris ke Row 16!]
svg.append(wire([(ox + 4*cw, oy + 14*rh), (ox + 4*cw, oy + 15*rh), (ox + 9*cw, oy + 15*rh)], "#3b82f6", width=4.5))

# 16. MAX98357A GND (Col 7, Row 15) -> Common GND (Col 10, Row 22 / DRV GND Row 20 Col 7)
svg.append(wire([(ox + 7*cw, oy + 14*rh), (ox + 6.4*cw, oy + 14*rh), (ox + 6.4*cw, oy + 19*rh), (ox + 7*cw, oy + 19*rh), (ox + 7*cw, oy + 21*rh), (ox + 9*cw, oy + 21*rh)], "#10b981", width=4.5, dash="6,3"))

# 17. DRV IN1 (Col 7, Row 17) -> GPIO 11 (Col 10, Row 17) [Lurus sebaris!]
svg.append(wire([(ox + 7*cw, oy + 16*rh), (ox + 9*cw, oy + 16*rh)], "#06b6d4", width=5))

# 18. DRV IN2 (Col 7, Row 18) -> GPIO 12 (Col 10, Row 18) [Lurus sebaris!]
svg.append(wire([(ox + 7*cw, oy + 17*rh), (ox + 9*cw, oy + 17*rh)], "#0ea5e9", width=5))

# 19. DRV IN3 (Col 7, Row 21) -> GPIO 14 (Col 10, Row 20)
svg.append(wire([(ox + 7*cw, oy + 20*rh), (ox + 8*cw - 5, oy + 20*rh), (ox + 8*cw - 5, oy + 19*rh), (ox + 9*cw, oy + 19*rh)], "#6366f1", width=4.5))

# 20. DRV IN4 (Col 7, Row 22) -> GPIO 13 (Col 10, Row 19)
svg.append(wire([(ox + 7*cw, oy + 21*rh), (ox + 8*cw + 18, oy + 21*rh), (ox + 8*cw + 18, oy + 18*rh), (ox + 9*cw, oy + 18*rh)], "#8b5cf6", width=4.5))

# 21. 5V BOOST POWER RAIL (MH-CD42 -> Row 21 Col 10 5Vin, DRV VCC Row 19 Col 7, Amp Vin Row 15 Col 8)
p_5v = [
    (ox + 9*cw, oy + 20*rh),
    (ox + 7*cw, oy + 18*rh),
    (ox + 7.6*cw, oy + 18*rh),
    (ox + 7.6*cw, oy + 14*rh),
    (ox + 8*cw, oy + 14*rh)
]
svg.append(wire(p_5v, "#f43f5e", width=5, dash="8,4"))

# ==================== BOTTOM CARD: MH-CD42 & LEGEND ====================
by = oy + 22 * rh + 55
svg.append(f'''
<g filter="url(#shadow)">
  <rect x="50" y="{by}" width="1420" height="240" rx="14" fill="#1e293b" stroke="#334155" stroke-width="2"/>
  <text x="80" y="{by + 36}" class="title" font-size="20">🔋 DISTRIBUSI DAYA MH-CD42 (IP5306) &amp; RINGKASAN AUDIT KELISTRIKAN</text>

  <!-- Power block -->
  <rect x="80" y="{by + 58}" width="400" height="158" rx="10" fill="#0f172a" stroke="#f43f5e" stroke-width="1.8"/>
  <text x="100" y="{by + 88}" class="legend-title" fill="#fca5a5">⚡ MODUL CHARGER / STEP-UP 5V</text>
  <text x="100" y="{by + 114}" class="legend-text">• BAT+ &amp; BAT- ➔ Baterai Li-Ion 3.7V (18650)</text>
  <text x="100" y="{by + 137}" class="legend-text">• VOUT+ (5.0V) ➔ Pin 5Vin ESP32, VCC Motor, Vin Amp</text>
  <text x="100" y="{by + 160}" class="legend-text">• VOUT- (GND) ➔ Common Ground (Rel Bawah R21/R22)</text>
  <text x="100" y="{by + 183}" class="legend-text" fill="#4ade80">✓ IP5306 Bebas Auto-Sleep: Beban ESP32 ~120mA stabil</text>

  <!-- Audit block -->
  <rect x="505" y="{by + 58}" width="470" height="158" rx="10" fill="#0f172a" stroke="#10b981" stroke-width="1.8"/>
  <text x="525" y="{by + 88}" class="legend-title" fill="#6ee7b7">🛡️ AUDIT INTEGRITAS SIGNAL &amp; PIN CONSTRAINT</text>
  <text x="525" y="{by + 114}" class="legend-text">• Sinyal Terpasang: LCD SDA = GPIO 4 | LCD SCK = GPIO 5</text>
  <text x="525" y="{by + 137}" class="legend-text">• MAX98357A: Vin ke 5V | GND ke GND | SD &amp; GAIN (Open)</text>
  <text x="525" y="{by + 160}" class="legend-text">• Strapping Safe: GPIO 0, 3, 45, 46 dibiarkan KOSONG (0 load)</text>
  <text x="525" y="{by + 183}" class="legend-text">• PSRAM Safe: GPIO 35, 36, 37 tidak tersentuh (anti-crash)</text>

  <!-- Wiring Color Legend -->
  <rect x="1000" y="{by + 58}" width="445" height="158" rx="10" fill="#0f172a" stroke="#38bdf8" stroke-width="1.8"/>
  <text x="1020" y="{by + 88}" class="legend-title" fill="#7dd3fc">🎨 KODE WARNA JALUR KABEL</text>
  <circle cx="1030" cy="{by + 112}" r="5" fill="#f43f5e"/><text x="1045" y="{by + 116}" class="legend-text">Merah: 5.0V Boost (Motor, Amp, ESP 5Vin)</text>
  <circle cx="1030" cy="{by + 135}" r="5" fill="#ec4899"/><text x="1045" y="{by + 139}" class="legend-text">Pink: 3.3V LDO Bersih (LCD &amp; Mic)</text>
  <circle cx="1030" cy="{by + 158}" r="5" fill="#10b981"/><text x="1045" y="{by + 162}" class="legend-text">Hijau: Common GND (Bintang ground bersama)</text>
  <circle cx="1030" cy="{by + 181}" r="5" fill="#3b82f6"/><text x="1045" y="{by + 185}" class="legend-text">Biru/Kuning/Ungu: Sinyal Digital SPI, I2S &amp; PWM</text>
</g>
''')

svg.append('</svg>')

target_proj_svg = "/Users/reggi/Projects/rbot/rbot_perfboard_wiring.svg"
with open(target_proj_svg, "w") as f:
    f.write("\n".join(svg))

print(f"SVG written to {target_proj_svg}")
