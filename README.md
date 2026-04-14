# vxwm - Personal fork with systray patch

This repository is my personal build of [vxwm](https://codeberg.org/wh1tepearl/vxwm), itself a fork of [dwm](https://dwm.suckless.org/) by [suckless](https://suckless.org/).

Thanks to **wh1tepearl** for the strong vxwm base, especially the infinite tags system and modular architecture.

---

## What changed in this fork

This version includes the following customizations and improvements:

- **System tray support**: systray/XEmbed handling is integrated into the bar, including icon geometry and pinning support.
- **Full English comments in `vxwm.c`**: the main window manager source file is annotated with explanatory comments to make the code easier to read, maintain, and fork.
- **Custom terminal**: default terminal changed from `st` to `kitty`, bound to `Mod+Return`.
- **Bar style**: bar background set to pure black (`#000000`).
- **Font choice**: `JetBrains Mono Nerd Font` is used for the bar and dmenu.
- **Clean statusbar**: removed the vxwm version string from the default status line.
- **Media control keys**: added brightness controls with `brightnessctl` and audio controls with `wpctl`.

---

## Key features

- Infinite tags support inherited from vxwm
- Systray integration with XEmbed protocol
- Keyboard-driven tiling window management
- Customizable layouts and tag handling
- Intentionally documented C source for easier development

---

## Dependencies

- `libx11`
- `libxft`
- `libxinerama`
- `freetype2`
- `brightnessctl` (brightness hotkeys)
- `pipewire` + `wireplumber` (audio hotkeys via `wpctl`)
- `kitty` (default terminal)

---

## Installation

```bash
git clone https://codeberg.org/wh1tepearl/vxwm.git
cd vxwm
sudo make clean install
```

If you want to customize the configuration, edit `config.def.h` and then rebuild.

---

## Upstream references

- vxwm by wh1tepearl: https://codeberg.org/wh1tepearl/vxwm
- dwm by suckless: https://dwm.suckless.org/
