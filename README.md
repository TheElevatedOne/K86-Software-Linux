# Attack Shark K86 – Linux configuration software

![Static Badge](https://img.shields.io/badge/CLAUDE-VIBECODED?style=for-the-badge&logo=claude&label=VIBECODED&labelColor=%23333333&color=%23de7356) [![AUR Version](https://img.shields.io/aur/version/k86-software-bin?style=for-the-badge&logo=archlinux)
](https://aur.archlinux.org/packages/k86-software-bin) [![AUR Version](https://img.shields.io/aur/version/k86-software-git?style=for-the-badge&logo=archlinux)
](https://aur.archlinux.org/packages/k86-software-git)

A C++ rewrite of the Windows-only Attack Shark K86 driver
(`https://download.attackshark.pro/ATTACKSHARK/K86/ATTACKSHARKK86.zip`), based on reverse
engineering of its Electron UI. The protocol is documented in [docs/PROTOCOL.md](docs/PROTOCOL.md).

It consists of

* `k86core` – a dependency-free C++17 library (Linux `hidraw`),
* `k86ctl` – a command line tool,
* `k86-gui` – an optional GTK 3 GUI (plain GTK, no libadwaita).

Both K86 firmware families known to the original driver are supported:

| Model | USB id | Firmware id |
|---|---|---|
| K86 (YC3121) | `3151:4015` | 1168 |
| K86 (YC3123) | `3151:5002` | 2730 |

## Features

* lighting effects (mode, brightness, speed, direction/option, colour or rainbow), side light
* per-key colours for the "custom" effect, per-key animations
* key remapping (3 profiles) and Fn layer remapping, factory layouts, export/import; the K86's
  own functions (Bluetooth channels, Win lock, lighting and side light keys, …) have names
* macros (50 slots), bound to keys with `macro:N`
* LCD: upload PNG/JPEG/BMP and animated GIFs, clock sync, CPU/RAM/disk/network stats, erase
* active profile, debounce, sleep timers, option flags, factory reset
* raw command access

Not implemented: firmware / screen-controller upgrades, and configuration over the
2.4 GHz dongle or Bluetooth (the original routes those through its closed `iot_driver.exe`);
connect the keyboard with the USB cable.

## Build

```sh
sudo apt install build-essential cmake pkg-config libgtk-3-dev   # GTK only needed for the GUI
# Fedora: sudo dnf install gcc-c++ cmake pkgconf gtk3-devel
# Arch:   sudo pacman -S base-devel cmake gtk3
cmake -S . -B build
cmake --build build -j
ctest --test-dir build        # protocol tests and the GUI self-test
sudo cmake --install build    # installs k86ctl, k86-gui and the udev rule
```

Use `-DK86_BUILD_GUI=OFF` to build without GTK.

### Permissions

Install the udev rule so your user can open the keyboard without root:

```sh
sudo cp udev/70-attackshark-k86.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

## The GUI

`k86-gui` works like the original software. It finds the keyboard by itself (also when it is
plugged in later), reads every setting from it, and shows:

* **Keys** and **Fn layer** – a drawing of the K86. Click a key (Ctrl-click for several), then pick
  what it should do: another key (with Ctrl / Shift / Alt / Win for combinations), media keys,
  mouse buttons, a macro, lighting, side light, connection or profile functions. The change is
  written right away. Changed keys are outlined in blue; the Fn layer shows the factory functions
  in green.
* **Lighting** – effects of the backlight and side light with brightness, speed, direction and
  colour, applied as you change them, with a preview.
* **Per-key colours** – paint keys for the Custom effect (click or drag), then apply.
* **Macros** – the 50 macro slots, with an event editor, recording from the keyboard and
  adjustable delays.
* **Screen** – images and GIFs for the LCD, clock sync.
* **Settings** – debounce, sleep timers, option switches, firmware versions, factory reset.

The profile buttons at the top choose which profile you see and edit (★ marks the one the keyboard
uses; *Use on keyboard* switches it).

```sh
k86-gui                      # normal use
k86-gui --read-only          # read and look around, never write to the keyboard
k86-gui --dry-run=v1         # a simulated YC3121 (v2 = YC3123); writes are printed
k86-gui --self-test          # click through every page against the simulator and check the writes
```

## Command line examples

```sh
k86ctl list
k86ctl info
k86ctl light modes
k86ctl light set wave --brightness 4 --speed 3 --option 1 --rainbow
k86ctl light set static --color ff8800
k86ctl sidelight set breath --color 00ffff
k86ctl keys                                          # key slot names
k86ctl keymap set CapsLock LCtrl --layer 0
k86ctl keymap set F12 consumer:Calculator --fn
k86ctl keymap set Pause conn:BT2                     # K86 functions have names too
k86ctl macro set 0 "+LCtrl/5 C/20 -LCtrl" && k86ctl keymap set Pause macro:0
k86ctl keymap export layer0.txt && k86ctl keymap import layer0.txt
k86ctl perkey fill 000000 && k86ctl perkey set W=ff0000 A=ff0000 S=ff0000 D=ff0000
k86ctl light set custom --option 0
k86ctl screen upload cat.gif
k86ctl screen clock
k86ctl sysinfo --interval 1
k86ctl --dry-run=v1 light set breath                 # print the reports, no hardware
```

Run `k86ctl --help` and `k86ctl actions` for the full syntax.

## Testing and verification

`tests/golden/*.txt` were generated by executing the original driver's own JavaScript device
classes for both K86 firmwares against a recording HID stub (`tests/js/run.mjs`; the vendor
code is not redistributed here). `k86tests` checks that the C++ implementation produces
byte-identical feature reports for 33 (YC3121) / 36 (YC3123) operations.

To regenerate the golden files, extract the installer (`7z x "ATTACK SHARK K86 SOFT.exe"`,
then `7z x '$PLUGINSDIR/app-32.7z'`) and run
`node tests/js/run.mjs <extracted>/resources/app/dist/js tests/golden`.

### Hardware

Tested on a K86 YC3121 (`3151:4015`, firmware id 1168, USB version 0x0114) over USB: device
queries, lighting, side light, per-key colours and animation, key and Fn remapping (checked through the
input reports), macros, profile, debounce, sleep timers, options, LCD images and GIFs, clock
sync. The GUI's preload was checked read-only against the keyboard; its write paths are checked
by `k86-gui --self-test` against the protocol simulator. Factory reset and screen erase were not
tried, and the
YC3123 (`3151:5002`) is untested. See docs/PROTOCOL.md for the firmware timing this needed.

## Releases

Prebuilt binaries (`.tar.gz`) and `.deb`, `.rpm` and pacman packages for x86_64 and aarch64
are on the [releases page](https://github.com/TheElevatedOne/AttackSharkKeyboardSoftware/releases),
each with a `.sha256` file and a combined `SHA256SUMS` (`sha256sum -c SHA256SUMS`). The
packages install the udev rule, so no extra permission setup is needed.

To make a release: set the version in `CMakeLists.txt`, add its section to
[CHANGELOG.md](CHANGELOG.md), commit, then tag and push (`git tag 0.2.0 && git push origin
0.2.0`) and run the **Release** workflow from the Actions tab. It builds the newest tag (or
the one you enter), checks that it matches the CMake version, and creates or updates the
GitHub release with the changelog section as its notes.

## License

GPL-3.0, see [LICENSE](LICENSE). `third_party/stb/stb_image.h` is by Sean Barrett and is
public domain / MIT (see the end of that file). The icon in `packaging/icons` is the Attack Shark
logo from the official driver; it is Attack Shark's trademark and not covered by the GPL.

Not affiliated with Attack Shark or RongYuan. The original driver is not included (apart from a
few small helper functions in `tests/js/stub_index.js`); the golden test data is the output of
running it.
