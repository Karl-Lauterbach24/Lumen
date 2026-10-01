#!/usr/bin/env python3
"""Translated READMEs from one template.

    python tools/make_readmes.py

Writes README.<lang>.md for every language in docs/readme/<lang>.json and updates the
language bar in README.md (English, the full reference) and README.de.md. The translated
READMEs are the user-facing part (what Lumen is, downloads, features, legal note, plugins);
building, architecture and tests are documented in English only.
"""
import io
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "docs", "readme")
REPO = "https://github.com/Karl-Lauterbach24/Lumen"

# Order and native names as in the program (src/I18n.cpp)
LANGUAGES = [
    ("en", "English"), ("de", "Deutsch"), ("fr", "Français"), ("es", "Español"), ("it", "Italiano"),
    ("pt", "Português"), ("nl", "Nederlands"), ("pl", "Polski"), ("sv", "Svenska"), ("cs", "Čeština"),
    ("tr", "Türkçe"), ("uk", "Українська"), ("ru", "Русский"), ("ja", "日本語"), ("zh", "简体中文"), ("ko", "한국어"),
]

TEMPLATE = """<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – {tagline}

{bar}

{intro}

- **{player_window}** – {player_window_text}
- **{control_window}** – {control_window_text}

{plays}

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## {h_downloads}

{downloads_intro}

| {col_system} | {col_file} |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` ({installer}) · `.zip` ({portable}) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

{linux_note} {updates_text}

## {h_features}

- {f_discs}
- {f_cinema}
- {f_streaming}
- {f_cast}
- {f_3d}
- {f_output}
- {f_audio}
- {f_cd}
- {f_comfort}
- {f_languages}

## {h_protection}

{protection_text}

## {h_plugins}

{plugins_text}

## {h_more}

{more_text}

## {h_license}

{license_text}
"""


def readme_name(code):
    return "README.md" if code == "en" else f"README.{code}.md"


def bar(current):
    parts = []
    for code, name in LANGUAGES:
        parts.append(f"**{name}**" if code == current else f"[{name}]({readme_name(code)})")
    return " · ".join(parts)


def main():
    written = []
    for code, _ in LANGUAGES:
        src = os.path.join(DATA, f"{code}.json")
        if not os.path.isfile(src):
            continue
        strings = json.load(io.open(src, encoding="utf-8"))
        text = TEMPLATE.format(bar=bar(code), **strings).replace("(REPO)", f"({REPO})").replace("(REPO/", f"({REPO}/")
        with io.open(os.path.join(ROOT, readme_name(code)), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        written.append(code)
    # Sprachleiste der beiden ausführlichen READMEs
    for code in ("en", "de"):
        path = os.path.join(ROOT, readme_name(code))
        text = io.open(path, encoding="utf-8").read()
        name = dict(LANGUAGES)[code]
        new, n = re.subn(r"^.*\*\*" + re.escape(name) + r"\*\*.*\[.*\]\(README.*$", bar(code), text, count=1, flags=re.M)
        if n != 1:
            raise SystemExit(f"language bar not found in {readme_name(code)}")
        with io.open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(new)
    print("written:", ", ".join(written))


if __name__ == "__main__":
    main()
