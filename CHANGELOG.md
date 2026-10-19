# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

The release workflow publishes the section whose version matches the tag (`0.1.0` →
`## [0.1.0]`; a leading `v` is ignored) as the release notes, so add a section before tagging.

## [Unreleased]

## [0.2.0] - 2026-10-18

### Added

- New GUI that works like the original software: a drawing of the K86 (taken from the original's
  layout) on the Keys and Fn layer pages; click a key (Ctrl-click for several) and pick its new
  action from categories (keys and combinations, media, mouse, macro, lighting, side light,
  connection, system, profile). Changes are written right away.
- Lighting page with effect tiles, named directions, colour swatches and a preview; side light.
- Per-key colour painting (click or drag, fill, rainbow, pick from key).
- Macro editor with the 50 slots, recording from the keyboard, editable delays and a key picker.
- Everything is read from the keyboard when it is connected; the keyboard is found
  automatically, also when plugged in later. Device I/O runs in the background.
- Profile switcher: view and edit any profile, make one the active profile.
- Names for the K86 firmware functions on the factory Fn layer, from the K86 user manual:
  Bluetooth 1-3 and 2.4 GHz, Win lock, battery level, factory reset, backlight effect, colour,
  speed and on/off, side light effect, colour, brightness and speed, knob mode
  (`conn:BT1`, `sys:WinLock`, `side:NextEffect`, ... in `k86ctl`), plus `profile:1`-`profile:3`.
- Readable names everywhere ("Macro 3 (toggle)", "Win + D", "Page Up"); `k86ctl keymap show`
  prints them next to the action.
- `k86-gui --read-only` (nothing is ever written), `--page`, and `--self-test`, which clicks
  through every page against a simulated keyboard and checks the result (also run by `ctest`).
- Protocol simulator (`SimulatedTransport`) used by `--dry-run`, the self-test and the unit tests.
- The release workflow runs the GUI self-tests under Xvfb.

### Changed

- `k86-gui --dry-run` simulates a keyboard with state instead of echoing commands; writes are
  printed to the terminal.
- Settings show on/off switches with descriptions instead of raw numbers.

## [0.1.1] - 2026-10-18

### Added

- Attack Shark icon for the GUI's window and desktop entry.

## [0.1.0] - 2026-10-18

First release.

### Added

- `k86core` C++17 library for both K86 firmwares: YC3121 (`3151:4015`) and YC3123
  (`3151:5002`), over Linux `hidraw`.
- `k86ctl` command line tool and `k86-gui` GTK 3 GUI.
- Lighting effects, side light, per-key colours and per-key animations.
- Key and Fn layer remapping (3 profiles), factory layouts, export and import.
- Macros (50 slots).
- LCD: images and animated GIFs, clock sync, system stats (YC3123), erase.
- Profile, debounce, sleep timers, option flags, factory reset, raw commands.
- `--dry-run` mode that prints the reports instead of sending them.
- udev rule for access without root, and a desktop entry for the GUI.
- Protocol documentation (`docs/PROTOCOL.md`) and golden-report tests against the original
  driver.
- Packages for x86_64 and aarch64: `.tar.gz`, `.deb`, `.rpm` and pacman, with SHA-256 sums.

### Fixed (found testing on a YC3121 K86)

- Wait for the firmware to finish committing each write. Reads during the commit returned
  stale data, and reading too early could reboot the keyboard.
- `macro clear` did not send anything.
- The per-key animation effect is reported instead of causing an error, and per-key colours
  are no longer lost when written while an animation plays.
- Side light support on the YC3121, which the original driver does not offer.

[Unreleased]: https://github.com/TheElevatedOne/K86-Software-Linux/compare/0.2.0...HEAD
[0.2.0]: https://github.com/TheElevatedOne/K86-Software-Linux/compare/0.1.1...0.2.0
[0.1.1]: https://github.com/TheElevatedOne/K86-Software-Linux/compare/0.1.0...0.1.1
[0.1.0]: https://github.com/TheElevatedOne/K86-Software-Linux/releases/tag/0.1.0
