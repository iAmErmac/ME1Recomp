"""One-pass rename of a fresh copy of SoDRecomp's sources into ME1Recomp's names
(adapted from SoDRecomp's scripts/rename_dao_to_sod.py).

Identifiers only: SoDRecomp -> ME1Recomp, SOD_x -> ME1_x, Sod<Upper>x -> Me1..., sod_x -> me1_x,
sod<Upper>x -> me1..., the sod namespace, sod-<target> CMake targets, and file and folder names
built the same way. Shared Xenia names (dao_*, DAO_*) are untouched. Prose saying "SoD" stays.

usage: rename_sod_to_me1.py <folder>      (run once, on the fresh copy)
"""
import os
import re
import sys

TEXT = {'.c', '.cc', '.cpp', '.h', '.hpp', '.inl', '.hlsl', '.hlsli', '.xesl', '.xesli', '.txt', '.cmake',
        '.py', '.rc', '.md', '.in', '.json', '.toml', '.manifest'}

RULES = [
    (re.compile(r'SoDRecomp'), 'ME1Recomp'),
    (re.compile(r'sodrecomp'), 'me1recomp'),
    (re.compile(r'\bSOD_(?=\w)'), 'ME1_'),
    (re.compile(r'\bSod(?=[A-Z])'), 'Me1'),
    (re.compile(r'\bsod_(?=\w)'), 'me1_'),
    (re.compile(r'\bsod(?=[A-Z])'), 'me1'),
    (re.compile(r'\bsod-(?=[a-z])'), 'me1-'),
    (re.compile(r'\bsod\b'), 'me1'),
]


def rename(text):
    for pattern, repl in RULES:
        text = pattern.sub(repl, text)
    return text


def main():
    root = sys.argv[1]
    changed_files = renamed = 0
    # Contents first, then names (deepest paths first so parents rename last).
    for dirpath, _, files in os.walk(root):
        for name in files:
            if os.path.splitext(name)[1].lower() not in TEXT and name != 'CMakeLists.txt':
                continue
            path = os.path.join(dirpath, name)
            if os.sep + 'thirdparty' + os.sep in path or name.startswith('rename_'):
                continue  # SDL, antilag2, lmxxf-nr: not ours
            raw = open(path, 'rb').read()
            try:
                text = raw.decode('utf-8')
            except UnicodeDecodeError:
                continue
            new = rename(text)
            if new != text:
                open(path, 'wb').write(new.encode('utf-8'))
                changed_files += 1
    for dirpath, dirs, files in os.walk(root, topdown=False):
        if os.sep + 'thirdparty' in dirpath:
            continue
        for name in files + dirs:
            new = rename(name)
            if new != name:
                os.rename(os.path.join(dirpath, name), os.path.join(dirpath, new))
                renamed += 1
    print(f'{changed_files} files rewritten, {renamed} files/folders renamed')


if __name__ == '__main__':
    main()
