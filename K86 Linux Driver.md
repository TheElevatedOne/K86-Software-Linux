# Attack Shark K86 for Linux

Source code of the C++ rewrite: library, `k86ctl` command line tool, GTK 3 GUI, protocol notes and tests. The archive includes the git history, so the two commits can be pushed from your PC.

**AttackSharkKeyboardSoftware.zip**359 KB · 181 entries

SHA-256

83bbe68e65e8662e0e1ded7d44f3fb8df562dec940d595de51b9f5117ecea540

Branch

claude/attackshark-k86-reverse-engineer-g27ph6

Commits

2b0bef7 Replace the Qt GUI with a GTK 3 GUI\
0b58951 Add Linux C++ rewrite of the Attack Shark K86 configuration software

## Push from your PC

```
unzip AttackSharkKeyboardSoftware.zip
cd AttackSharkKeyboardSoftware
git log --oneline
git push -u origin claude/attackshark-k86-reverse-engineer-g27ph6
```

The remote points to `github.com/TheElevatedOne/AttackSharkKeyboardSoftware`. If your PC uses SSH, run `git remote set-url origin git@github.com:TheElevatedOne/AttackSharkKeyboardSoftware.git` first.

## Build

```
sudo apt install build-essential cmake pkg-config libgtk-3-dev
cmake -S . -B build
cmake --build build -j
ctest --test-dir build
sudo cp udev/70-attackshark-k86.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

## What is inside

- `src/core/` protocol library for both K86 firmwares (YC3121 `3151:4015`, YC3123 `3151:5002`)
- `src/cli/` `k86ctl`, with `--dry-run=v1|v2` to print reports without hardware
- `src/gui/` `k86-gui`, plain GTK 3
- `docs/PROTOCOL.md` reverse-engineered command reference
- `tests/` golden reports from the original driver's own code; the C++ output matches byte for byte
- `udev/` rule for non-root access