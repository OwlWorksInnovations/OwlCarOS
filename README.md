# OwlCarOS

A lightweight Linux car infotainment system for x86_64 hardware, built with Buildroot.

## Current project status

**Early development.** The SDL2/KMSDRM image boots in WSL QEMU at 1280x800 and detects input devices. The accelerated image (Mesa 26.0.1 with i915/iris/crocus, LLVM 21.1.8 and SDL2 OpenGL ES) was flashed to the 64 GB SanDisk on 2026-11-09 and read back byte-for-byte verified. Startup and display compatibility on target hardware, plus GPU acceleration on the target, still need testing. Bluetooth AVRCP, USB auto-mounting and the installer also need target testing.

The system currently includes:

- Buildroot 2026.02.3, Linux 6.18.7, BusyBox and glibc
- Mesa 26.0.1 OpenGL ES/EGL with the i915, iris and crocus Gallium drivers plus softpipe, using LLVM 21.1.8
- UEFI boot through GRUB, with an installer in the Settings screen and a console installer in the GRUB menu
- Wi-Fi drivers and wpa_supplicant
- BlueZ Bluetooth pairing and A2DP audio playback through bluealsa
- System menu with confirmed reboot/shutdown actions and a live CPU, memory, uptime, load and process monitor
- Bluetooth track title, artist and album read from BlueZ over D-Bus, with play/pause/previous/next sent to the phone (AVRCP)
- Media browser with folders, and a music library on its own FAT32 partition (`OWLMUSIC`)
- Automatic read-only mounting of USB FAT, ext, exFAT and NTFS3 partitions under `/run/media`, with one-step copying of their music into the library
- A full-screen SDL2 interface using KMS/DRM with keyboard navigation
- Local WAV and MP3 playback
- Audio utilities and a startup mixer level adjustment
- Battery percentage from Linux power-supply devices that identify as batteries; shows `--%` if no battery is detected

Every screen shares the red Audi TT (8N) inspired radio styling of the main menu: the lens frame, one palette and a list whose selected row is a solid bar. Installation to a real disk still needs testing.

## Controls

On the home screen, use Up/Down to select Media, Navigation, Phone or Settings, then Enter to open. Navigation and Phone are currently placeholders.

Settings offers Reboot, Shutdown, System Info and Install. Reboot and Shutdown open a confirmation list that starts on Cancel; move to the action and press Enter to run it. System Info shows live CPU and memory use, load average, uptime, task count and the five processes using the most memory.

### Media

The top of the Media screen shows what is playing. The list below it starts with the sources:

- **Bluetooth Audio** - the paired phone
- **Music Library** - music stored on the device
- **USB Drive (...)** - one entry per plugged-in USB partition
- **Copy USB Music to Library** - shown while a USB drive is plugged in
- **Built-in Sounds** - the test chime

| Key | Action |
| --- | --- |
| Up / Down, Page Up / Page Down | Move through the list |
| Enter | Open a folder, play a file, or select Bluetooth |
| Space, Play/Pause key | Pause or resume whatever is playing |
| Left / Right, Previous/Next keys | Previous or next track of the active source |
| Backspace or Esc | Up one folder; back to the menu from the sources |
| Volume keys, M | Volume and mute |

Folders are shown before files. Playing a file queues every audio file in that folder, in name order, and playback continues through the folder. WAV uses `aplay`; MP3 uses `mpg123`. Starting local playback pauses the phone and releases the sound card from Bluetooth until the folder ends.

The skip and play/pause keys follow the **active source**, not the highlighted row. While Bluetooth is playing they skip the track on the phone; while a local file is playing they move through its folder. The media keys work on every screen.

### Bluetooth

Bluetooth pairing is open under the name **OwlCarOS** and uses the BlueZ pairing agent. When the phone starts playing, the Media screen switches to Bluetooth by itself and shows the title, artist and album that the phone's player (Spotify, YouTube Music, ...) publishes over AVRCP. The UI reads this from BlueZ's `org.bluez.MediaPlayer1` D-Bus object and calls its `Play`, `Pause`, `Next` and `Previous` methods. A phone or app that publishes no player shows "Bluetooth Audio" and reports that it offers no remote control.

### Getting music onto the device

Music is kept in `/root/Music`. On the first boot from a freshly flashed USB stick, OwlCarOS creates a FAT32 partition labelled `OWLMUSIC` in the unused space of the stick and mounts it there. There are two ways to fill it:

1. **From a computer.** Plug the OwlCarOS stick into a Windows, macOS or Linux computer. The `OWLMUSIC` drive appears next to the boot partition; copy folders of MP3 or WAV files onto it.
2. **From another USB drive, in the car.** Plug in a drive with music, open Media and choose **Copy USB Music to Library**. WAV and MP3 files are copied with their folders; files already in the library are skipped, and copying stops when the library is full. Progress is shown on that row.

USB drives can also be played directly without copying. They are mounted read-only at `/run/media/<device>` when inserted and unmounted when removed. FAT, ext2/3/4, exFAT and NTFS3 are supported. Only USB storage is mounted this way; internal drives and the drive OwlCarOS runs from are never listed as USB media.

The music partition starts 8 GiB into the stick, clear of the OS image, so flashing a newer OwlCarOS image keeps the music: the next boot finds the existing filesystem and reuses it. A system installed to an internal disk has no spare space after the root partition, so `/root/Music` is a normal folder on the root filesystem there. Details are logged to `/var/log/owlmusic.log`.

## Installing to an internal drive

Boot from the USB stick, open **Settings** and choose **Install - Copy OwlCarOS to an Internal Drive**.

1. Pick the drive. Every drive of 4 GB or more is listed with its model and size, except the stick OwlCarOS is running from. SATA, NVMe and eMMC drives are supported.
2. Confirm. The list starts on Cancel; move to **Erase the Drive and Install** and press Enter.
3. Wait. The screen shows each step and a progress bar. Do not power off or remove the stick.
4. When it reports **Installation complete**, choose **Shut Down Now**, remove the stick and power on.

**The selected drive is erased completely.** Check the model and size before confirming.

The installer creates an EFI boot partition and a root partition that fills the rest of the drive, copies the running system onto it, copies the music library from the stick if it fits, and points GRUB at the new root partition. The work is done by `/sbin/owlcaros-install`, which logs to `/var/log/owlcaros-install.log`.

The stick's GRUB menu also starts this installer directly as **OwlCarOS Installer (Graphical)**, which boots into the Settings install screen instead of the home screen. The original console installer is still there as **OwlCarOS Installer (Console)**. Both have been tested in QEMU only.

### Music on an installed system

On an internal drive the library is the folder `/root/Music` on the root partition, which has the whole drive to itself. Music gets there in two ways:

- The installer copies whatever is in the stick's library at install time.
- Later, plug in any USB drive with music, open Media and choose **Copy USB Music to Library**. This works the same as on the stick.

## Audio notes

The kernel includes Intel HD Audio support and common codecs. If the mixer starts muted or very low, the UI attempts to unmute a playback control and set it to 60%.

For a QEMU audio test, add:

```sh
-audiodev pa,id=aud0 -device ich9-intel-hda -device hda-duplex,audiodev=aud0
```

For headless QEMU use `-audiodev none,id=aud0`. The interface runs on `tty1`; use `Ctrl+Alt+F2` for a debug shell. UI logs are at `/var/log/owlcaros-ui.log`.

## Remaining work

- Test Bluetooth track info and skip/pause with the target phone (Spotify and YouTube Music), across pause, track changes and disconnects
- Test music-partition creation on the target USB stick, and copying music onto it from Windows
- Test USB insertion/removal, direct playback and library import from FAT, exFAT and NTFS3 drives on target hardware
- Validate the Settings installer on a spare internal disk and verify that it boots
- Wi-Fi hotspot connectivity
- Online maps and navigation
- Functional Phone screen and additional system settings
- Theme selection (TT red, B5 amber and other cluster variants)
- Verify battery reporting on the target laptop
- Split the UI source into focused display, input, and screen modules for easier maintenance

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
    │   └── storage.config
    └── rootfs-overlay/
        ├── sbin/
        │   ├── owlcaros-install
        │   ├── owlcaros-installer-init
        │   ├── owlcaros-music-import
        │   ├── owlcaros-rootdisk
        │   └── owlcaros-usb-media
        └── etc/
            ├── bluetooth/main.conf
            ├── inittab
            └── init.d/
                ├── S12owlmusic
                ├── S42bt-agent
                ├── S45bluealsa
                ├── S46bluealsa-aplay
                ├── S48usb-media
                └── S99owlcaros-ui
    └── staging/EFI/BOOT/grub.cfg
```
