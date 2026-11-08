# OwlCarOS

A custom, lightweight Linux-based car infotainment operating system.

OwlCarOS is being developed for a Celeron x86_64 laptop, with the goal of providing a high performance and early 2000s car infotainment interface.

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
- Automatic driver loading via BusyBox mdev (Wi-Fi/Bluetooth modules, USB hotplug)
- A2DP playback through `bluealsa-aplay`, with discoverable/pairable mode
- ALSA audio utilities (aplay/amixer) and MP3 playback via mpg123
- Framebuffer/DRM console and display support
- Native framebuffer UI shell (`owlcaros-ui`) with keyboard navigation
- Red Audi TT (8N) inspired dot-matrix instrument-cluster theme
- Media player for local WAV/MP3 files and Bluetooth audio
- Boots straight into the dashboard; debug console on `tty2` and serial

## UI controls

`owlcaros-ui` draws directly to `/dev/fb0` (no X11/Wayland) and reads
`evdev` input, so any keyboard works. The look is inspired by the red
dot-matrix cluster of a 2004 Audi TT (8N): black panels, red LEDs and
warm white markings.

Home screen:

- Arrow keys: move between Media, Navigation, Phone and Settings
- Enter: open the highlighted tile
- Volume up/down and mute keys: adjust the system mixer

Media screen:

- Up/Down: select the Bluetooth entry or a local track
- Enter or Space: play/pause the selection (Enter on Bluetooth starts A2DP)
- Left/Right: previous/next track
- Volume up/down, M: volume and mute
- Esc or Backspace: back to the home screen

Local music is discovered in `/media`, `/run/media`, `/mnt`,
`/root/Music` and `/usr/share/owlcaros/media`. WAV files play through
`aplay`, MP3 files through `mpg123`. A short theme chime ships as a test
track.

When a local track starts, the UI pauses the Bluetooth A2DP player so
both never fight over the ALSA device, then restarts it when playback
ends.

The car shows up as **OwlCarOS** and is always discoverable and pairable.
A `bt-agent` pairing agent (bluez-tools) accepts phones with "Just Works"
pairing, so no PIN or on-screen confirmation is needed. If a phone tried
to pair with an older build, forget the old entry on the phone first.

If a phone pairs but drops the connection or never offers media audio,
check that `bluetoothctl show` lists `Audio Sink`. BlueZ only links its
A2DP/AVRCP plugins at build time, and Buildroot does not regenerate the
plugin list when `BR2_PACKAGE_BLUEZ5_UTILS_PLUGINS_AUDIO` is toggled on an
existing build. Rebuild BlueZ from scratch after changing its options:

```sh
make bluez5_utils-dirclean bluez-alsa-dirclean all
```

## Audio

The Intel HD Audio driver and its generic, Realtek, Cirrus and HDMI
codecs are built into the kernel, so the sound card is ready as soon as
the kernel boots.

A fresh ALSA state can leave the mixer muted or at 0%. On startup the UI
picks the best playback control (`Master`, then `PCM`, `Speaker`, ...)
and, if it is below 20%, unmutes it and sets it to 60%.

To test audio in QEMU, add an emulated HDA card to the usual command line:

```sh
-audiodev pa,id=aud0 -device ich9-intel-hda -device hda-duplex,audiodev=aud0
```

Use `-audiodev none,id=aud0` for headless runs. Then check from the debug
shell:

```sh
cat /proc/asound/cards
amixer sget Master
aplay /usr/share/owlcaros/media/owlcaros-theme.wav
```

The UI runs on `tty1`. Press `Ctrl+Alt+F2` for a debug shell (or use the
serial console). Runtime information is written to
`/var/log/owlcaros-ui.log`; restart the UI with:

```sh
/etc/init.d/S99owlcaros-ui restart
```

## Planned features - feel free to make a pull request or issue if you want to add/suggest a feature/support

- Bluetooth track metadata and AVRCP controls
- USB media auto-mounting
- Wi-Fi hotspot connectivity
- Online maps and navigation
- Full infotainment applications inside the native UI shell
- Theme selection (TT red, B5 amber, cluster variants)

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
│       ├── tools/
│       │   └── make_chime.py
│       └── src/
│           ├── main.c
│           ├── Makefile
│           ├── LICENSE
│           └── media/
│               └── owlcaros-theme.wav
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
        │       ├── bluetooth/main.conf
        │       ├── inittab
        │       └── init.d/
        │           ├── S42bt-agent
│           ├── S45bluealsa
        │           ├── S46bluealsa-aplay
        │           └── S99owlcaros-ui
        └── staging/
            └── EFI/BOOT/
                └── grub.cfg
```
