# Attack Shark K86 configuration protocol

Reverse engineered from the official Windows driver
`ATTACKSHARKK86.zip` → `ATTACK SHARK K86 SOFT.exe` (NSIS installer) →
`$PLUGINSDIR/app-32.7z` → Electron app "Attack Shark Driver v4" 3.1.12 ("GearHub").

## Software architecture of the original

* `resources/app/dist/` – React UI. All device logic (command bytes, packet layouts) lives
  in this JavaScript. One class per firmware; the K86 classes are
  `Yc3121_sg8967_3m_soc` (chunk `b74085e4.js`, base class `CommonKbYc500` in `438d24dc.js`)
  and `Yc3123_sg_sg8967_3m_1k` (chunk `95287242.js`, base classes in `63fce7a0.js` and
  `5e635fe2.js`).
* `resources/app/iot_driver.exe` – a Rust (tonic) gRPC-web server on `127.0.0.1:3814`
  (`driver.DriverGrpc`: `sendMsg`, `readMsg`, `sendRawFeature`, `readRawFeature`,
  `watchDevList`, …). It only moves bytes between the UI and HID devices.
* The UI also contains a WebHID fallback (`sendFeature` / `readFeature`) that shows the
  exact framing; this port implements that path on Linux `hidraw`.

## Devices

| Name in driver | Chip | VID:PID | Firmware id (cmd 0x8F) | Profiles | Extra |
|---|---|---|---|---|---|
| `yc3121_sg8967_3m_soc` | YC3121 | 3151:4015 | 1168 | 3 | 240×135 LCD, side light (see below) |
| `yc3123_sg_sg8967_3m_1k` | YC3123 | 3151:5002 | 2730 | 3 | 240×135 LCD, side light, Win/Mac Fn layers |

Other keyboards use the same VID:PID, so the firmware id is checked before writing.

The configuration interface is the HID collection with **usage page 0xFFFF, usage 0x02**.

## Framing

All traffic is 64-byte **feature reports with report ID 0** (on Linux: `HIDIOCSFEATURE` /
`HIDIOCGFEATURE` with a leading 0 byte). A command is sent with SET_FEATURE; its answer is
fetched with GET_FEATURE. The original waits 10 ms before each send and each read.

The message is zero padded to at least 8 bytes, then a checksum is applied:

* **Bit7** (most commands): `byte[7] = 255 - (sum(byte[0..6]) & 0xFF)`
* **Bit8** (lighting writes): `byte[8] = 255 - (sum(byte[0..7]) & 0xFF)`

Bulk writes use an 8-byte header (byte 7 = checksum) followed by up to 56 data bytes.
After most writes the original pauses ("vendor sleep"): 500 ms (YC3121), 100 ms (YC3123).

### Write commit (observed on a YC3121, firmware id 1168, USB version 0x0114)

The vendor sleep is not enough. After a write the firmware keeps committing it to flash; while
it does, every SET/GET_FEATURE takes ~100 ms instead of ~1 ms. Measured end of that window,
counted from the first packet of the write:

| Write | Busy until |
|---|---|
| single key `13` | ~0.5 s |
| backlight `07` | ~1.1 s |
| per-key colours `0C` ×7 | ~1.7 s |
| key / Fn matrix `09` / `10` ×9 | ~2.1 s |

* Reads inside the window return **stale pages** (old data, no error), so a read-modify-write
  right after a write silently writes old data back.
* Reading **~10 ms after a write rebooted the keyboard** (USB disconnect, re-enumeration; the
  unsaved write and the current effect were lost). Polling from 500 ms on was safe.
* Responses to the paged reads (`89`, `8C`, …) carry no header, so stale data cannot be
  recognised by content.

This port waits for the window to close after every YC3121 write: after the vendor sleep it
repeats the device id query `8F` until two in a row complete in under 60 ms (at most 4 s).
Not verified on the YC3123, where it is off.

## Commands

`P` = profile/layer, `S` = key slot (0–127), `pg` = page.

| Function | YC3121 (K86 v1) | YC3123 (K86 v2) |
|---|---|---|
| Device id | `8F` → `[0]=8F`, id = u32 LE at [1] | same |
| Firmware version | `80` → [2]<<8\|[1] | `8F` → [8]<<8\|[7]; RF: `80` → [2]<<8\|[1] |
| Screen / LED MCU versions | `AD` → [1..2], flash [3..4]; `AE` → [1..2] | same |
| Factory reset | `02` (then 4 s) | `01` (64 bytes, then 2 s) |
| Set / get profile | `05 P` / `85` → [1] | `04 P` / `84` → [1] |
| Set / get debounce | `11 00 ms` / `91` → [2] | `06 ms` / `86` → [1] |
| Set / get sleep | `12`, u16 LE at [8..15] / `92` → [1..8] | `11`, [8..15] / `91` → [8..15] |
| Set / get backlight (Bit8) | `07 …` / `87` | same (different speed scale) |
| Set / get side light (Bit8) | `08 …` / `88` (not used by the original) | `08 …` / `88` |
| Options | `06 00 flags fn pwr` / `86 00` → [2..4] | `09 00 fnLayer anti rt/25 wasd` / `89` → [1..5]; `85` → [1] |
| Key matrix read | `89 P pg` ×8 → 512 bytes | `8A P FF pg 00` ×8 |
| Key matrix write | `09 P F8 01 pg` + 56 ×9 | `0A P FF pg len last 00` + 56 ×10 |
| Single key write | `13 P S` …, action at [8..11] | `0A P S 00 00 01 00`, action at [8..11] |
| Fn matrix read | `90 P pg` ×8 | `90 os P FF pg` ×8 (os 0 = Win, 1 = Mac) |
| Fn matrix write | `10 P F8 01 pg` + 56 ×9 | `10 os P FF pg len last` + 56 ×10 |
| Single Fn key | `15 P S`, action at [8..11] | `10 os P S`, action at [8..11] |
| Macro write | `16 idx pg 38 last` + 56 | `0B idx pg 38 last` + 56 |
| Macro read | `8B idx pg` (up to 4 pages) | same |
| Per-key colours write | `0C 00 80 01 pg` + 56 ×7 | `0C P FF pg len last` + 56 ×7 |
| Per-key colours read | `8C 00 pg` ×6 → 384 bytes | `8C P FF pg` ×6 |
| Per-key animation | start `18`; frame `19 pg frame count delay` + 56 ×7 | start `12`; frame `12 frame pg 01 count delayLo delayHi` + 56 ×7 |
| LCD upload announce | `A5 frame count delay lenLo lenMid 00 · L T R B L>>8 T>>8 R>>8 B>>8 len>>16 len>>24 00`; repeat until answer [1] ≠ 0; [1] = 1 → accepted | same |
| LCD data | `25 frame count delay pgLo pgHi len` + 56 | same |
| Clock | `28`, [8]=year>>8 [9]=year&FF [10]=month [11]=day [12..14]=h m s | same |
| System info | `22`, u16 LE GB disk free/total, RAM used/total at [8..15], CPU % [16], CPU °C [17], net up/down GB [18..21] | same |
| Screen language switch | `27 01` | same |
| Erase screen flash | `AC` → `AC AA AA 55 55` | same |

### Backlight packet (`07`, Bit8)

`[07, mode, speedByte, brightness, opt<<4 | colourType, R, G, B]`

* `speedByte = MAXSPEED - speed` (MAXSPEED = 5 on YC3121, 4 on YC3123; UI range 0–4).
* `colourType` = 7 single colour, 8 rainbow. Music modes (20, 22): 4 single, 0 rainbow.
  Custom picture (13): low nibble 0 and RGB forced to `00 C8 C8`. Screen colour (21): byte = 0.
* Pure white `FFFFFF` is written as `FAFFFA` (and mapped back when read).
* Read-back: a low nibble 0–6 means a preset colour (red, orange, yellow, green, cyan, blue, purple).

Modes: 0 off, 1 static, 2 breath, 3 neon, 4 wave, 5 ripple, 6 raindrop, 7 snake,
8 reactive, 9 converge, 10 sine wave, 11 kaleidoscope, 12 line wave, 13 custom,
14 laser, 15 circle wave, 16 dazzle, 17 rain down, 18 meteor, 19 reactive-off,
20 music 3, 21 screen colour, 22 music 2, 23 train, 24 fireworks.
The K86 UI offers all of them except 20, 23, 24 (and "off" is not in its list).

After a per-key animation upload (`18`/`19`) the YC3121 reports mode **66 (`0x42`)**, which the
driver does not list. Per-key colour writes (`0C`) made in that mode read back fine but are
discarded when the effect changes, so this port switches to "custom" (13) before writing them.

Side light (`08`): the original only sends it to the YC3123, but the YC3121 K86 has a side light
that answers the same packets (mode, speed and colour checked on hardware). Modes 0 off, 1 static, 2 breath, 3 neon (type byte 8), 4 wave,
5 snake, 6 music; speed is not inverted (0–3).

### Key actions (4 bytes per slot, 128 slots)

| Bytes | Meaning |
|---|---|
| `00 k1 k2 k3` | keyboard usages pressed together (plain key `00 00 u 00`) |
| `01 00 b x` | mouse: 240 left, 241 right, 242 middle, 243 back, 244 forward, `F5 01`/`F5 FF` wheel |
| `02 00 u 00` | system control (81 power, 82 sleep, 83 wake) |
| `03 00 lo hi` | consumer usage (E2 mute, E9 vol+, EA vol−, CD play/pause …) |
| `08 00 op n` | profile switching: op 1 next, 2 previous, 3 cycle, 4 select profile n |
| `09 type idx 00` | macro idx; type 0 = repeat N, 1 = toggle, 2 = while held |
| `0A 01 00 00` | Fn |
| `00 00 00 00` | disabled |

### K86 firmware functions

The default Fn layers also use firmware functions that the original software does not name (its
UI shows them as "System Key"). Their meaning comes from matching each slot of the factory Fn layer
with the Fn key list of the K86 user manual
(`https://download.attackshark.pro/ATTACKSHARK/K86/K86UserManual.pdf`):

| Bytes | Factory key | Function (manual) | `k86ctl` name |
|---|---|---|---|
| `0A 02 00 00` | Fn+Esc | hold 3 s: factory reset | `sys:FactoryReset` |
| `0A 03 00 00` | Fn+Win | lock the Win key | `sys:WinLock` |
| `0A 08 00 00` | Fn+Backspace | show the battery level on the number row | `sys:Battery` |
| `0A 0C 00 00` | Fn+] (YC3121) | backlight on/off, power saving | `led:OnOff` |
| `0A 09 00 00` | Fn+] (YC3123) | same key in the YC3123 table (assumed the same function) | `led:OnOff2` |
| `0E 00 00/01/02 00` | Fn+1/2/3 | Bluetooth 1/2/3 (hold to pair) | `conn:BT1` … `conn:BT3` |
| `0E 00 05 00` | Fn+4 | 2.4 GHz (hold to pair) | `conn:2.4G` |
| `0D 01 00 00` | Fn+\ | next backlight effect | `led:NextEffect` |
| `0D 05 01 00` | Fn+Enter | next backlight colour | `led:NextColor` |
| `0D 02 01/02 00` | Fn+Up/Down | backlight brightness +/− (named `LEDBIRINC/DEC` by the driver) | `led:BrightnessUp/Down` |
| `0D 03 01/02 00` | Fn+Left/Right | backlight speed −/+ | `led:SpeedDown/Up` |
| `0D 08 00 00` | Fn+C | next side light effect | `side:NextEffect` |
| `0D 08 03 00` | Fn+V | next side light colour | `side:NextColor` |
| `0D 08 01 00` | Fn+Z | side light brightness cycle | `side:Brightness` |
| `0D 08 02 00` | Fn+X | side light speed cycle | `side:Speed` |
| `13 03 00 00` | Fn+knob press | switch the knob between screen menu and volume | `knob:Mode` |

The YC3123 base layer also has `13 01 00 00` and `13 01 01 00` in slots without a printed key
(probably knob functions); they stay unnamed (`raw:`).

### Factory Fn layer

The YC3121 tested here ships with a different Fn layer than the driver's default table: Fn+F1–F8
are `00 00 00 00` and Fn+F9–F12 are consumer PrevTrack / PlayPause / NextTrack / MediaPlayer
(`0x183`). `keymap reset --fn` writes the driver's table, not this one.

### Macro slot (256 bytes)

`repeat (u16 LE)` followed by events:

* key / mouse button, delay ≤ 127 ms: `code, delay | 0x80 if down`
* key / mouse button, longer delay: `code, 0x80 if down else 0, delay u16 LE`
* mouse move: `F9, delay (≤127) or 0, dx, dy [, delay u16 LE if long]`

The list ends at four zero bytes. The original only sends the chunks that contain non-zero
bytes; for an empty macro that would be no packet at all, so this port always sends the first one. Notes on the original: it encodes a 0 ms delay in the
short form (which its own decoder then misreads), and decodes a short mouse-move delay as
`byte >> 1` although it stores it unshifted. This port writes 0 ms delays in the long form
and reads the move delay unshifted.

### LCD images

Images are scaled to fit 240×135 (aspect preserved, centred), cropped to the bounding box
of non-black pixels and sent as **RGB565 big-endian in column-major order**. Static images go
to slot 0–4 (`frame` = slot, `count` = 1). Animations are announced once with the first
frame's header and `count` = number of frames, then each frame is streamed with its index;
the frame delay byte is in milliseconds (0–255).
