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
- Login prompt on both the local display and serial console

## Planned features - feel free to make a pull request or issue if you want to add/suggest a feature/support

- Bluetooth audio streaming from Android
- ALSA audio output
- Wi-Fi hotspot connectivity
- Online maps
- Native C-based infotainment interface
- 1990s / early-2000s automotive-inspired design

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
        │           └── S45bluealsa
        └── staging/
            └── EFI/BOOT/
                └── grub.cfg
```
