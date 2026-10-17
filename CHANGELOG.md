# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

The release workflow publishes the section whose version matches the tag (`v0.1.0` →
`## [0.1.0]`) as the release notes, so add a section before tagging.

## [Unreleased]

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

[Unreleased]: https://github.com/TheElevatedOne/AttackSharkKeyboardSoftware/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/TheElevatedOne/AttackSharkKeyboardSoftware/releases/tag/v0.1.0
