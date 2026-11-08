# OwlCarOS

A custom, lightweight Linux-based car infotainment operating system.

OwlCarOS is being developed for an celeron x86_64 laptop, with the goal of
providing a simple, efficient and retro-inspired car infotainment interface. _possible theme support_

## Current status

**Early development — bootable baseline achieved.**

Current features:

- Buildroot 2026.02.3
- Linux kernel 6.18.7
- x86_64 architecture
- GRUB2 UEFI bootloader
- BusyBox userspace
- musl C library
- 256 MB ext4 root filesystem
- Custom OwlCarOS system identity
- Automated disk image generation
- QEMU UEFI boot tested

## Planned features (feel free to make a pull request or issue if you want to add/suggest a feature/support)

- Bluetooth audio streaming from Android
- ALSA audio output
- Wi-Fi hotspot connectivity
- Online maps
- Native C-based infotainment interface
- Retro 1990s / early-2000s automotive-inspired design

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
        └── staging/
            └── EFI/BOOT/
                └── grub.cfg
