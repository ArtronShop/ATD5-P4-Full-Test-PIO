# Fonts

Generated with [lv_font_conv](https://github.com/lvgl/lv_font_conv) (`--bpp 4 --no-compress --format lvgl --lv-include lvgl.h`).

- `gui_font_th*` – Noto Sans Thai (OFL), Thai range `0x0E01-0x0E5B` + Latin `0x20-0x7E,0xB7,0xD7,0x2013,0x2022,0x2026`
  (sizes 16/20/24, bold 24/28/48).
- `gui_font_icon*` – Font Awesome 5 Free (solid + the brand `usb` glyph `0xF287`), sizes 24/40/72.
  Glyph list: see `../gui_icons.h`.

To add a glyph/character, re-run lv_font_conv with the extra range and update `gui_icons.h`.
