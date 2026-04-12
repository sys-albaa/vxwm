# vxwm - my personal build

This is my personal build of [vxwm](https://codeberg.org/wh1tepearl/vxwm), a fork of [dwm](https://dwm.suckless.org/) by [suckless](https://suckless.org/).

A huge thanks to **wh1tepearl** for the incredible work on vxwm — the infinite tags system, the various modules and the overall quality of the codebase made this fork worth building on top of.

---

## My personal changes

These are the modifications I made on top of the original vxwm codebase to fit my personal workflow:

- **Terminal**: changed default terminal from `st` to `kitty`, bound to `Mod+Return`
- **Bar color**: changed the bar background color to pure black (`#000000`)
- **Font**: switched from `monospace` to `JetBrains Mono Nerd Font` for both the bar and dmenu
- **Version string**: removed the vxwm version string from the statusbar
- **Media keys**: added keybindings for brightness (`XF86MonBrightnessUp/Down`) and audio (`XF86AudioRaiseVolume`, `XF86AudioLowerVolume`, `XF86AudioMute`) using `brightnessctl` and `wpctl`

---

## Dependencies

- libx11
- libxft
- libxinerama
- freetype2
- brightnessctl (for brightness control)
- pipewire + wireplumber (for audio control via wpctl)
- kitty (default terminal)

---

## Installation

```bash
git clone https://codeberg.org/wh1tepearl/vxwm.git
cd vxwm
sudo make clean install
```

---

## Original project

- vxwm by wh1tepearl: https://codeberg.org/wh1tepearl/vxwm
- dwm by suckless: https://dwm.suckless.org/
