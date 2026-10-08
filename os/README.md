# LumenOS

Lumen as a player of its own: a small Debian system that starts straight into Lumen, full screen, to
be used from the sofa. It plays discs from any drive the kernel knows (SATA, USB), files from its
internal disk, from USB storage and from network shares, copies discs to any of these with MakeMKV if
you set that up, and keeps Lumen up to date by itself.

The images are attached to Lumen's releases: `LumenOS-<version>-amd64.iso` for PCs (Intel/AMD, UEFI
and BIOS) and `LumenOS-<version>-arm64.iso` for ARM machines that start through UEFI.

## Using it

1. Write the image to a USB stick (`dd`, balenaEtcher, Rufus in "DD" mode) and start the machine
   from it. LumenOS runs from the stick.
2. *Settings › Install on this device* puts it on a disk of the machine (the disk is erased). What
   you set up while running from the stick – remotes, network, shares – comes along.

Everything is operated with arrow keys, OK and Back.

| | |
|---|---|
| **Keyboard and mouse** | work at once: arrows, Enter, Esc. |
| **Remotes and gamepads** (USB, Bluetooth, infrared, the television's remote through HDMI-CEC) | When one appears that Lumen does not know, it asks for its buttons: *press the button for Up*, and so on – six that are needed, thirteen more that can be skipped. After that the device just works, also the next time it is plugged in. *Settings › Remotes* sets one up again. |
| **Bluetooth** | *Settings › Bluetooth* searches and pairs. |
| **Network** | A cable is taken by itself; *Settings › Network* joins a Wi-Fi network (on-screen keyboard). |
| **Network shares** | *Settings › Network shares* adds SMB and NFS shares; they appear in the library and as targets for copying. |
| **USB storage** | appears in the library when plugged in (FAT, exFAT, NTFS, ext4, …). |

**Discs.** A disc is mounted when it is inserted and plays with its menu (*Settings › Playback* turns
both off). Unencrypted discs play as they are. For encrypted ones LumenOS brings Debian's `libaacs`
and `libbdplus` – libraries without any keys – and nothing more: Lumen ships neither keys nor
decryption. *Settings › Discs* is where you decide what else this device gets:

- a key file of your own for Blu-rays (`KEYDB.cfg` in the top folder of a USB stick),
- `libdvdcss` for DVDs, built on the device by Debian's `libdvd-pkg`,
- MakeMKV, loaded from makemkv.com and built on the device, for playing and for copying. MakeMKV is
  GuinpinSoft's program with a licence of its own, which the page asks you to accept.

**Copying a disc** (*Disc › Copy to storage*) lets MakeMKV write the films of the disc as MKV files
into a folder named after the disc, on the internal disk, a USB disk or a share.

**Updates.** Every six hours (and four minutes after starting) LumenOS asks GitHub for Lumen's latest
release, loads the package for its machine, checks it against the release's `SHA256SUMS.txt`, waits
until nothing is playing or copying, installs it and starts the player again. *Settings › Update*
shows what is happening, looks at once, or turns this off. Debian's security updates install
themselves (`unattended-upgrades`).

**Maintenance.** Ctrl+Alt+F2 opens a console: user `lumen`, password `lumen` (change it with
`passwd`), `sudo` for everything. `journalctl -u lumenos-session` is the player's log;
`/etc/lumenos/session.env` takes environment variables for it (`LUMEN_MPV_LOG=v`, `LUMEN_PERF_LOG=1`).

## How it is made

LumenOS is Debian 13 with a handful of packages, and Lumen. Everything specific to it comes out of
Lumen's own Debian package, so an update of Lumen updates the system's part too:

| in the package | |
|---|---|
| `lumen --os` | the interface for the television (`qml/Os*.qml`), remotes (`src/InputMapper`), storage and the bridge to the system (`src/OsBridge`), copying (`src/RipManager`) |
| `share/lumen/os/lumenos-session` | the one session: the compositor `cage` with Lumen in it, on the first console |
| `share/lumen/os/lumenos-admin` | the only thing Lumen may do with the system's rights (through one `sudo` rule): network, Bluetooth, shares, update, power, installing |
| `share/lumen/os/lumenos-update` | the updater (a systemd timer) |
| `share/lumen/os/lumenos-disc`, `lumenos-storage`, `udev/90-lumenos.rules` | mounting discs and pluggable storage |
| `share/lumen/os/lumenos-install` | installing to a disk |
| `share/lumen/os/lumenos-setup` | makes a Debian system into the player: user, services, rules. Run when the image is built and after every update |

`lumenos-setup install` also turns a plain Debian 13 with Lumen installed into LumenOS.

## Building the image

On Debian 13, with `live-build` installed and the Lumen package for the architecture at hand:

```sh
sudo os/image/build.sh amd64 Lumen-<version>-linux-amd64.deb out/
sudo os/image/build.sh arm64 Lumen-<version>-linux-arm64.deb out/
```

Building the other architecture needs `qemu-user-static`. `os/image/packages.list` names what goes
in besides Debian's minimal base.

## Limits

- **ARM:** the image starts on machines with UEFI firmware (servers, many boards with EDK2 or U-Boot's
  UEFI, virtual machines). A Raspberry Pi needs UEFI firmware on its card to start it; there is no
  image for the Pi's own boot chain yet.
- **HDMI-CEC** needs an adapter the kernel drives (Raspberry Pi, some Intel NUCs, Pulse-Eight's USB
  adapter). Most PC graphics cards have none.
- Bluetooth devices that want a number typed for pairing (keyboards) are not handled; remotes and
  gamepads pair without.
- Picture: SDR. Lumen's own window is used, which does not pass HDR through.
- Reading ahead of a disc (see the notes of Lumen 1.4.1) covers Blu-rays; DVDs play with the small
  buffer they always had.
