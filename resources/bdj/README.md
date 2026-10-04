# libbluray's Java archive (BD-J menus)

Disc menus written in Java (BD-J) are run by libbluray in a Java VM. For that libbluray loads its own Java
classes: `libbluray-j2se-<version>.jar` and `libbluray-awt-j2se-<version>.jar`, **in exactly the version of
the libbluray in use**. Distributions ship them as a separate package (`libbluray-bdj`); Homebrew and MSYS2
do not ship them at all, so Lumen's macOS and Windows packages bring them along.

| File | For libbluray | Built from |
|------|---------------|------------|
| `libbluray-j2se-1.5.0.jar`, `libbluray-awt-j2se-1.5.0.jar` | 1.5.0 | [`libbluray-1.5.0.tar.xz`](https://download.videolan.org/pub/videolan/libbluray/1.5.0/libbluray-1.5.0.tar.xz), SHA-256 `f676408e91a5d321abf8b8d4dfdae36205c297dab5c54c3ec519639025f474a2` |

Built with [`tools/build_bdj.sh`](../../tools/build_bdj.sh) (javac and jar of a JDK, no ant; the same sources
and the same split into two archives as libbluray's own `build.xml`), unchanged sources, class files for
Java 7:

```
tools/build_bdj.sh libbluray-1.5.0.tar.xz
```

- The build picks the pair that matches the libbluray it found: macOS puts it into the bundle
  (`Contents/Resources/bdj`), Windows next to `lumen.exe` (`bdj\`, `tools/deploy_windows.py`). When libbluray
  moves to a new version, CMake and the deploy script say that the archive is missing; build it with the
  script and add it here.
- Linux packages use the distribution's libbluray and its `libbluray-bdj` package.
- `tools/test_bdj.sh` runs a Java menu of a generated disc through libbluray with these archives.

Licence: libbluray, LGPL-2.1-or-later (see [THIRD_PARTY.md](../../THIRD_PARTY.md)). The archive contains the
ASM library (`org.objectweb.asm`), BSD-3-Clause: [LICENSE.asm.txt](LICENSE.asm.txt).
