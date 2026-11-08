# OwlCarOS

A lightweight Linux car infotainment system for x86_64 hardware, built with Buildroot.

## Current project status

**Early development.** The Buildroot image builds and has booted in QEMU. A recent image was prepared for USB testing. The latest installer and radio-style interface have not yet been fully validated on the target laptop and a separate installation disk.

The system currently includes:

- Buildroot 2026.02.3, Linux 6.18.7, BusyBox and musl
- UEFI boot through GRUB, with an automated disk installer
- Wi-Fi drivers and wpa_supplicant
- BlueZ Bluetooth pairing and A2DP audio playback through bluealsa
- A native framebuffer interface with keyboard navigation
- Local WAV and MP3 playback
- Audio utilities and a startup mixer level adjustment
- Battery percentage from Linux power-supply devices that identify as batteries; shows `--%` if no battery is detected

The home and media screens use the red Audi TT (8N) inspired radio styling. The installer is intended to install to an internal disk while protecting the boot USB. It asks the user to type the target device path before erasing it. Installation to a real disk still needs testing.

## Controls

On the home screen, use Up/Down to select Media, Navigation, Phone or Settings, then Enter to open. Navigation, Phone and Settings are currently placeholders.

In Media, Up/Down selects a source or track, Enter/Space plays or pauses, Left/Right selects the previous or next track, and Esc/Backspace returns home. Volume keys and M control volume and mute.

Local tracks are searched under `/media`, `/run/media`, `/mnt`, `/root/Music` and `/usr/share/owlcaros/media`. WAV uses `aplay`; MP3 uses `mpg123`. Starting local playback pauses Bluetooth playback until the local track ends.

Bluetooth pairing is open under the name **OwlCarOS** and uses the BlueZ pairing agent. Bluetooth A2DP playback is available. AVRCP track metadata and media controls are not implemented yet. USB storage hotplug detection exists through mdev, but drives are not automatically mounted yet.

## Installing to a hard drive

Boot the USB image and launch the installer from its GRUB menu. The installer lists available disks and excludes the boot USB. It requires typing the exact confirmation string for the selected device before it repartitions the disk, copies the system, expands the root filesystem, and updates the installed boot entry.

**The selected disk is erased.** Check the displayed model and size carefully. The installer has not yet been validated on a real target disk.

## Audio notes

The kernel includes Intel HD Audio support and common codecs. If the mixer starts muted or very low, the UI attempts to unmute a playback control and set it to 60%.

For a QEMU audio test, add:

```sh
-audiodev pa,id=aud0 -device ich9-intel-hda -device hda-duplex,audiodev=aud0
```

For headless QEMU use `-audiodev none,id=aud0`. The interface runs on `tty1`; use `Ctrl+Alt+F2` for a debug shell. UI logs are at `/var/log/owlcaros-ui.log`.

## Planned work

- Bluetooth track metadata and AVRCP controls
- USB media auto-mounting and removal handling
- Wi-Fi hotspot connectivity
- Online maps and navigation
- Functional Phone and Settings screens
- Theme selection (TT red, B5 amber and other cluster variants)

## Repository structure

```text
OwlCarOS/
├── Config.in
├── external.desc
├── external.mk
├── README.md
├── configs/
│   └── owlcaros_x86_64_defconfig
├── package/
│   └── owlcaros-ui/
│       ├── Config.in
│       ├── owlcaros-ui.mk
│       ├── tools/make_chime.py
│       └── src/
│           ├── main.c
│           ├── Makefile
│           └── media/owlcaros-theme.wav
└── board/owlcaros/
    ├── post-build.sh
    ├── post-image.sh
    ├── genimage.cfg
    ├── linux/
    ├── rootfs-overlay/
    │   ├── sbin/owlcaros-installer-init
    │   └── etc/
    │       ├── bluetooth/main.conf
    │       ├── inittab
    │       └── init.d/
    │           ├── S42bt-agent
    │           ├── S45bluealsa
    │           ├── S46bluealsa-aplay
    │           └── S99owlcaros-ui
    └── staging/EFI/BOOT/grub.cfg
```
