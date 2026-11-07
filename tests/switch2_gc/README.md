# Native GameCube host checks

The five golden-vector C test files are adapted from PicoSwitch2 revision
`e054bfd1311289f392069ff0d2447e01748c2124` under Apache-2.0. Input model symbols
were renamed to the Joypad port's names. Source and license attribution:
[protocol port](../../src/usb/usbd/modes/switch2_gc/README.md),
[upstream license](../../src/usb/usbd/modes/switch2_gc/LICENSE.PicoSwitch2).

`test_transport.c` and `usb_stub.h` exercise the complete production protocol
and Joypad transport with mocked USB and clock I/O. The Python runner supports
MSVC or a host cc/GCC/Clang compiler; this session validated with MSVC on Windows.
The hardware acceptance checklist is in `docs/SWITCH2_NSO_GAMECUBE.md`.
