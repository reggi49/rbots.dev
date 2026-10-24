# Watch Face Personalization & Theme Architecture

**Project:** R-bot Companion Application & ST7735 Kawaii Cat Face Engine  
**Display Hardware:** ST7735 128×160 RGB565 SPI  
**Date:** October 2026  

---

## 1. Expressions vs. Base Themes

In R-bot, **an expression is not a theme**:
- **Base Theme (Watch Face Studio):** Configures the default idle character styling, eye specular highlights, accessory colors, and resting expressions.
- **Runtime Expression:** Dynamically triggered by the conversational AI pipeline (e.g., `LISTENING`, `THINKING`, `SPEAKING`, `HAPPY`).
- **Core Principle:** Personalizing a base theme via Watch Face Studio **never disables or overrides** automatic `LISTENING`, `THINKING`, or `SPEAKING` animation when the user talks to R-bot.

---

## 2. Theme-to-Firmware Mapping

| Theme ID | Display Title | Description | Primary Mood / Visual Characteristic |
|:---|:---|:---|:---|
| `kawaii_classic` | Kawaii Classic | Default anime pupil gradients, rose bow, sweet :3 smile | `FACE_NEUTRAL` / `FACE_IDLE` |
| `happy_cat` | Happy Cat | Joyful crescent closed eyes (^ ^), smiling mouth, raised paws | `FACE_HAPPY` / `FACE_HAPPY_BIG` |
| `sleepy_cat` | Sleepy Cat | Resting eyes on pink cushion, floating Zzz, dimmed backlight | `FACE_SLEEPY` |
| `love_cat` | Love Cat | Twinkling heart-shaped pupils, drifting hearts, strong blush | `FACE_LOVE` |
| `curious_cat` | Curious Cat | Asymmetrical alert eyes, raised left/right paw, inquisitiveness | `FACE_CURIOUS_L` / `FACE_CURIOUS_R` |
| `playful_cat` | Playful Cat | Wink arc with mischievous tongue blep (:P) | `FACE_PLAYFUL` |
