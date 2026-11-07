# RP2040 Pro Micro: XInput to native NSO GameCube USB

Build target: `joypad_usb_promicro_nso_gc`. This is a separate native Nintendo
GameCube Controller USB personality (VID 057E, PID 2073), using PicoSwitch2's
protocol engine at revision `e054bfd1311289f392069ff0d2447e01748c2124`.
The existing `joypad_usb_promicro` adapter target and its source are unchanged
by this port. Its current source still includes the earlier v3-v5 compatibility
experiments; the independently hardware-validated v2 rollback UF2 remains at
`build/rumble-diagnostic/joypad_promicro_gc_rumble_fix_v2.uf2`.

## Build and connect

Install an ARM embedded GCC toolchain with newlib, CMake, Ninja and Python 3,
then initialize the pinned dependencies and configure from the repository root:

```sh
git submodule update --init --recursive
cmake -S src -B build -G Ninja -DPICO_BOARD=pico -DPICO_TINYUSB_PATH="$PWD/src/lib/pico-sdk/lib/tinyusb"
ninja -C build joypad_usb_promicro_nso_gc
```

In PowerShell, replace `$PWD` with `$($PWD.Path)` in that configure command.
The known working configuration uses pico-sdk 2.2.0 and its bundled TinyUSB
0.18.0. Select it explicitly: the upstream Makefile instead selects the separate
TinyUSB checkout, which was not used for this hardware-validated build.
For an already configured Windows build, run:

```powershell
ninja -C build joypad_usb_promicro_nso_gc
```

Output: `build/joypad_usb_promicro_nso_gc.uf2`.
The target uses the existing RP2040 configuration and GP2 D+/GP3 D- USB-host
wiring. Connect the 8BitDo receiver in its working XInput configuration to the
host port, and the board's native USB port to Switch 2. It always boots into
native NSO GC mode, ignoring any saved output-mode choice, without changing
that saved choice. A mode-cycling button is not required.

No hardware was flashed as part of implementation. Keep the v2 rollback file
before manually installing this test build. The existing build uses a Pico
2MB flash layout; this change does not repartition the Pro Micro's flash.

## Mapping

| Joypad input | Native output |
| --- | --- |
| B1 / B2 / B3 / B4 | A / B / X / Y |
| S1 (XInput Back) | Z |
| L1 / R1 | L / R final digital clicks, also forcing corresponding travel to 100% |
| S2 (XInput Start) | Plus / Start |
| S1 + S2 together (Z + Start) | ZL / Suspend Menu, consuming Z and Plus while held |
| Start + D-pad Up | Home, consuming Start and Up while held |
| Start + D-pad Down | Minus, consuming Start and Down while held |
| A1 / A2, when available | Home / Capture |
| Profile L2 / R2 analog | Continuous L / R travel when the corresponding final click is released |

Sticks use centered 12-bit Nintendo axes, with HID-to-Nintendo Y inversion.
D-pad directions pass through. A final L/R click now forces the corresponding
analog byte to 255, even if the controller supplies no analog travel. Releasing
the click restores the profile's analog value immediately. Each side is handled
separately. This intentionally replaces the earlier strictly independent travel
mapping, at the user's request, only in native NSO mode. There are no analog
thresholds or L2/R2-to-detent fallbacks. The legacy adapter mode is unchanged.
It represents one
controller. Native report 0x0A carries independent clicks and analog travel;
the reference's PC-selected report 0x05 carries travel but has no detent slots.

## Profile interactions

The existing router and profile pipeline still runs before this output mode.
Custom remapping, combos, axis changes, thresholds and trigger behaviors can
therefore change the input presented to it. In particular:

- `profile_apply()` synthesizes 255 from digital L2/R2 plus zero analog, even
  without an active built-in profile; it has another fallback after processing.
- Built-in profiles can remap buttons, override analog values, disable/cap travel,
  or make full travel from a digital trigger. Custom router profiles/overrides
  can remap and threshold trigger bits before that step.
- The router's global shoulder swap swaps L1/L2 and R1/R2 **and overwrites analog
  travel with 0/255 from the swapped digital state**. Its preceding source
  comment says analog is untouched, but the actual code does overwrite it.

For gradual XInput triggers, use passthrough settings and disable shoulder swap.
The XInput parser itself produces separate analog LT/RT and no threshold-derived
digital L2/R2 bits. It and the lower USB-host driver were left unchanged.

## Validation and remaining hardware checks

```powershell
python -B -m unittest discover -s tests -p 'test_*gc*.py'
ninja -C build joypad_usb_promicro_nso_gc joypad_usb_promicro
```

Host checks compile the actual native protocol and transport with mocked USB
and clock I/O, plus upstream report, identity, report-selection, rumble and AES
golden vectors. They cover all 256 trigger values with each click combination
and release, analog travel without synthesized clicks,
menu mapping, neutral/endpoints, initialization, EP0 identity, USB pairing,
busy/failure retries, truncated commands, report selection and session reset.
The legacy adapter descriptor/protocol tests also run. Full ARM builds pass;
the existing `MBEDTLS_CONFIG_FILE` redefinition warning remains.

Validated on 2026-11-06: nine Python tests pass (including five C golden-vector
suites and the full native transport harness). Final UF2: 1,342,976 bytes,
RP2040 family, flash payload range `0x10000000..0x100A3F00`, SHA-256
`0d5ed0c232563420c38dfa60fab6adef9e8b48e253182dd5c341556f06fd9b7a`.
The file does not write the earlier diagnostic log area at `0x10100000`.

The user confirmed that the initial native build works on Switch 2, appears as
a GameCube controller, and plays correctly. The user reports that it does not
work in Windows; Windows compatibility remains unresolved and is not part of
the menu-shortcut change. The confirmed native artifact is preserved as
`build/rumble-diagnostic/joypad_promicro_nso_gc_v1_working.uf2`.

Nintendo documents **ZL** as the native GameCube Suspend Menu control; native Z
occupies the logical ZR slot. See the [official controller FAQ](https://en-americas-support.nintendo.com/app/answers/detail/a_id/68609).
New menu shortcuts still require hardware testing. Regression checks:

1. Console recognizes a native GameCube controller after a cold connection.
2. In NSO Sunshine on the same save, gradual R gives spray while moving, then
   the final R click permits stationary aiming without losing water spray.
3. Verify released zero, partial/full L, face buttons, Z, Start, sticks and D-pad.
4. Verify Start+Z Suspend, Start+Up Home, Start+Down Minus, rumble and rumble stop, receiver disconnect and
   reconnect, then console disconnect/reconnect and a fresh initialization.

Motion/audio and Bluetooth output are outside this implementation. The protocol
port retains upstream synthetic calibration, opaque compatibility replies and
40-byte SPI-read clamp; compatibility remains subject to hardware testing.
Protocol provenance and port differences are documented beside the source in
`src/usb/usbd/modes/switch2_gc/README.md`.
