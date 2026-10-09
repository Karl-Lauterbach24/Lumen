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
2. The first start leads through a short **setup**, in any of Lumen's 16 languages:
   - the language,
   - a remote or gamepad, if one is connected (its buttons are asked for – see below),
   - *Install on this device*: puts LumenOS on a disk of the machine (the disk is erased), or go on
     trying it from the stick,
   - the network: a cable is taken by itself, a Wi-Fi network is chosen here,
   - the **update**: with a network, LumenOS looks for a newer Lumen at once and installs it,
   - **encrypted discs**: three switches, all off until you turn them on (see *Discs*).

   *Settings › Run the setup again* shows it again; every step is also a page of the settings.

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
both off). Unencrypted discs play as they are. Almost every Blu-ray and DVD on sale is encrypted, and
whether getting around that is allowed – even to play a disc you own – depends on where you live.
LumenOS therefore installs nothing for it by itself. The setup and *Settings › Discs* offer three
switches:

- **Blu-ray: key database.** The database its community keeps (FindVUK) for Debian's `libaacs`. The
  image carries it as it was on the day the image was made, not installed; switched on, it is put in
  place and from then on refreshed once a week when there is a network. `/etc/lumenos/keydb.url` names
  another source. A key file of your own (`KEYDB.cfg` in the top folder of a USB stick) can be taken
  instead.
- **DVD: libdvdcss.** The image carries the library as a Debian package that is not installed (built
  with Debian's `libdvd-pkg` while the image is made); switched on, it is installed from there, without
  a network.
- **MakeMKV**, for playing and for copying. MakeMKV is GuinpinSoft's program with a licence of its
  own that does not allow passing it on, so it is never in the image: switched on – after the page
  with its licence – it is loaded from makemkv.com and built on the device (ten minutes and more, needs
  a network).

Each switch takes back what it set up, and none of them restarts the player: Lumen looks for the
system's libraries again whenever one of these jobs has finished.

**Copying a disc** (*Disc › Copy to storage*) lets MakeMKV write the films of the disc as MKV files
into a folder named after the disc, on the internal disk, a USB disk or a share.

**Updates.** Every six hours (and four minutes after starting, and in the setup) LumenOS asks GitHub
for Lumen's latest release, loads the package for its machine, checks it against the release's
`SHA256SUMS.txt`, waits until nothing is playing, copying or being set up, installs it and starts the
player again. *Settings › Update* shows what is happening, looks at once, or turns this off. Debian's
security updates install themselves (`unattended-upgrades`).

**Updating without a network.** Put one of these into the top folder of a USB stick and plug it in –
LumenOS offers it, and *Settings › Update › Update from a USB stick* lists it:

| on the stick | what it updates |
|---|---|
| `Lumen-<version>-linux-<arch>.deb` from the release page | everything |
| `LumenOS-update-<version>.zip` from the release page (both architectures, with checksums) | everything |
| the source code as GitHub hands it out (*Code › Download ZIP*, `Lumen-main.zip`) | the interface (`qml/`), the translations, the icons and LumenOS's own scripts |

The source code cannot replace the player's core – that is compiled – so a zip of it is taken as an
overlay: the pages, texts and scripts in it go before those of the installed package (kept in
`/var/lib/lumenos/overlay`), until the next package replaces both. `os/system/API` in the source says
what the interface asks of the core; a zip that asks for more than the installed core has is not taken,
and the page says that a package is needed.

**Maintenance.** Ctrl+Alt+F2 opens a console: user `lumen`, password `lumen` (change it with
`passwd`), `sudo` for everything. `journalctl -t lumenos-session` is the player's log;
`/etc/lumenos/session.env` takes `NAME=value` lines for its environment (`LUMEN_MPV_LOG=v`,
`LUMEN_PERF_LOG=1`, `QSG_INFO=1`).

## How it is made

LumenOS is Debian 13 with a handful of packages, and Lumen. Everything specific to it comes out of
Lumen's own Debian package, so an update of Lumen updates the system's part too:

| in the package | |
|---|---|
| `lumen --os` | the interface for the television (`qml/Os*.qml`), remotes (`src/InputMapper`), storage and the bridge to the system (`src/OsBridge`), copying (`src/RipManager`) |
| `share/lumen/os/lumenos-session` | the one session: the compositor `cage` with Lumen in it, on the first console |
| `share/lumen/os/lumenos-admin` | the only thing Lumen may do with the system's rights (through one `sudo` rule): network, Bluetooth, shares, update, power, installing |
| `share/lumen/os/lumenos-update` | the updater (a systemd timer) |
| `share/lumen/os/lumenos-offline` | updates from a USB stick: package, update zip, source zip |
| `share/lumen/os/lumenos-makemkv` | loads and builds MakeMKV when asked, reads its beta key |
| `share/lumen/os/lumenos-disc`, `lumenos-storage`, `udev/90-lumenos.rules` | mounting discs and pluggable storage |
| `share/lumen/os/lumenos-install` | installing to a disk |
| `share/lumen/os/lumenos-setup` | makes a Debian system into the player: user, services, rules. Run when the image is built, after every update, and – a small part – early at every start (the machine's name, name resolution) |

`lumenos-setup install` also turns a plain Debian 13 with Lumen installed into LumenOS.

## Building the image

On Debian 13, with `live-build` installed and the Lumen package for the architecture at hand:

```sh
sudo os/image/build.sh amd64 Lumen-<version>-linux-amd64.deb out/
sudo os/image/build.sh arm64 Lumen-<version>-linux-arm64.deb out/
```

Building the other architecture needs `qemu-user-static`. `os/image/packages.list` names what goes
in besides Debian's minimal base. The two things the image carries for encrypted discs without
installing them (`/usr/share/lumenos-optional`: the libdvdcss package, the key database of that day)
are left out with `LUMENOS_NO_DVDCSS=1` and `LUMENOS_NO_KEYDB=1`; the device then builds or loads
them itself when asked. The repository holds neither – only an image built from it does.

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
- **No graphics driver** (a virtual machine, a board Mesa has no driver for): the interface is drawn by
  Qt's software renderer and the film by Mesa's; fine for trying LumenOS out, too slow for films in
  full resolution.
- A drive or a share that stops answering is left out of the library after six seconds and comes back
  when it answers again; the interface does not wait for it. A film disc that never opens is given up
  after two minutes (Lumen 1.4.1). What a drive that hangs needs in the end is to be unplugged.
