# OwlCarOS

A custom, lightweight Linux-based car infotainment operating system.

OwlCarOS is being developed for an celeron x86_64 laptop, with the goal of providing a high performance and early 2000s car infotainment interface. _possible theme support_

## Current status

**Early development**

Current features:

- Buildroot 2026.02.3
- Linux kernel 6.18.7
- x86_64 architecture
- GRUB2 UEFI bootloader
- BusyBox userspace
- musl C library
- 512 MB ext4 root filesystem
- Custom OwlCarOS system identity
- Automated disk image generation
- QEMU UEFI boot tested
- Broad Wi-Fi driver support (incl. wpa_supplicant and regulatory database)
- Bluetooth audio support (BlueZ + bluealsa A2DP sink)
- ALSA audio utilities (aplay/amixer)
- Framebuffer/DRM console and display support
- Native framebuffer UI shell (`owlcaros-ui`) with keyboard navigation
- Boots straight into the dashboard; debug console on `tty2` and serial

## UI controls

`owlcaros-ui` draws directly to `/dev/fb0` (no X11/Wayland) and reads
`evdev` input, so any keyboard works.

- Arrow keys: move between the four tiles
- Enter: select the highlighted tile
- Volume up/down and mute keys: adjust the status bar

The UI runs on `tty1`. Press `Ctrl+Alt+F2` for a debug shell (or use the
serial console). Runtime information is written to
`/var/log/owlcaros-ui.log`; restart the UI with:

```sh
/etc/init.d/S99owlcaros-ui restart
```

## Planned features - feel free to make a pull request or issue if you want to add/suggest a feature/support

- Bluetooth audio streaming from Android
- ALSA audio output
- Wi-Fi hotspot connectivity
- Online maps
- Full infotainment applications inside the native UI shell
- 1990s / early-2000s automotive-inspired design and themes

## Repository structure

```text
OwlCarOS/
├── Config.in
├── external.desc
├── external.mk
├── README.md
├── .gitignore
├── configs/
│   └── owlcaros_x86_64_defconfig
├── package/
│   └── owlcaros-ui/
│       ├── Config.in
│       ├── owlcaros-ui.mk
│       └── src/
│           ├── main.c
│           ├── Makefile
│           └── LICENSE
└── board/
    └── owlcaros/
        ├── post-build.sh
        ├── post-image.sh
        ├── genimage.cfg
        ├── linux/
        │   ├── wifi.config
        │   ├── bluetooth.config
        │   └── display.config
        ├── rootfs-overlay/
        │   └── etc/
        │       ├── inittab
        │       └── init.d/
        │           ├── S45bluealsa
        │           └── S99owlcaros-ui
        └── staging/
            └── EFI/BOOT/
                └── grub.cfg
```
