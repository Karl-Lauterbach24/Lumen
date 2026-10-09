# Sounds of LumenOS's interface

| file | when |
|---|---|
| `move.flac` | the selection moves |
| `key.flac` | a key of the on-screen keyboard |
| `select.flac` | something is opened or started |
| `back.flac` | one page back |
| `on.flac`, `off.flac` | a switch |
| `error.flac` | something did not work |
| `done.flac` | a longer job has finished |
| `start.flac` | LumenOS has started |
| `music.flac` | the ambient loop in the menus (72 seconds; the end is blended into the beginning) |

The files are rendered by `tools/make_sounds.sh` from a small synthesizer (`tools/soundgen`), so they
are Lumen's own and under its licence.

**Your own sounds:** LumenOS plays what it finds, first in `/etc/lumenos/sounds`, then here
(`/usr/share/lumen/sounds`). Put a file with one of the names above into `/etc/lumenos/sounds` – FLAC,
WAV, Ogg Vorbis/Opus or MP3, any sample rate – and it is used instead after the next start of Lumen.
An empty file of that name silences the sound.
