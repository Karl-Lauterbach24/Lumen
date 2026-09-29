#!/usr/bin/env python3
"""Übersetzungen der Oberfläche pflegen.

    python tools/i18n.py update     # Texte aus qml/, src/, profiles/presets.json sammeln,
                                    # i18n/<sprache>.json ergänzen (neue Einträge leer),
                                    # veraltete entfernen
    python tools/i18n.py check      # fehlende Übersetzungen je Sprache anzeigen
    python tools/i18n.py add xx     # neue Sprache anlegen (danach in src/I18n.cpp eintragen)

Quellsprache ist Deutsch. Ein Wörterbuch ist ein JSON-Objekt {"Quelltext": "Übersetzung"};
leere Werte fallen zur Laufzeit auf Englisch bzw. den deutschen Text zurück.
Übersetzbar sind qsTr("…") in QML, LTR("…") und QT_TRANSLATE_NOOP("Lumen", "…") in C++
sowie Name und Beschreibung der Vorlagen in presets.json.
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, 'i18n')
STR = r'"((?:[^"\\]|\\.)*)"'
PATTERNS = {
    '.qml': [re.compile(r'qsTr\(\s*' + STR)],
    '.cpp': [re.compile(r'\bLTR\(\s*' + STR), re.compile(r'QT_TRANSLATE_NOOP\(\s*"Lumen"\s*,\s*' + STR)],
}


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            n = s[i + 1]
            out.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'"}.get(n, '\\' + n))
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def collect():
    keys = []
    seen = set()

    def add(k):
        if k and k not in seen:
            seen.add(k)
            keys.append(k)

    for ext, pats in PATTERNS.items():
        for f in sorted(glob.glob(os.path.join(ROOT, 'qml' if ext == '.qml' else 'src', '*' + ext))):
            text = open(f, encoding='utf-8').read()
            for p in pats:
                for m in p.finditer(text):
                    add(unescape(m.group(1)))
    for p in json.load(open(os.path.join(ROOT, 'profiles', 'presets.json'), encoding='utf-8')):
        add(p.get('name', ''))
        add(p.get('description', ''))
    return keys


def load(lang):
    path = os.path.join(DIR, lang + '.json')
    return json.load(open(path, encoding='utf-8')) if os.path.exists(path) else {}


def save(lang, data):
    with open(os.path.join(DIR, lang + '.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
        f.write('\n')


def languages():
    return sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(DIR, '*.json')))


def update():
    keys = collect()
    for lang in languages():
        old = load(lang)
        new = {k: old.get(k, '') for k in keys}
        save(lang, new)
        dropped = len([k for k in old if k not in new])
        print(f'{lang}: {len(keys)} Texte, {sum(1 for v in new.values() if not v)} offen, {dropped} entfernt')


def check():
    keys = collect()
    bad = 0
    for lang in languages():
        data = load(lang)
        missing = [k for k in keys if not data.get(k)]
        bad += len(missing)
        print(f'{lang}: {len(keys) - len(missing)}/{len(keys)} übersetzt')
        for k in missing[:20]:
            print('   -', k)
    return 1 if bad else 0


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'check'
    os.makedirs(DIR, exist_ok=True)
    if cmd == 'update':
        update()
    elif cmd == 'add' and len(sys.argv) > 2:
        save(sys.argv[2], {k: '' for k in collect()})
        update()
    else:
        sys.exit(check())
