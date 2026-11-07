# Native NSO GameCube protocol port

Derived from [PicoSwitch2](https://github.com/notsosaelin/PicoSwitch2/tree/e054bfd1311289f392069ff0d2447e01748c2124),
commit `e054bfd1311289f392069ff0d2447e01748c2124`, under Apache-2.0.
The upstream license is retained as `LICENSE.PicoSwitch2`. Derived files are
marked with their source revision. No root upstream NOTICE file was present.

Sources ported: `firmware/src/console/gamecube/*`, the shared
`firmware/src/bluetooth/ns2_pairing_crypto.c/.h`, and the small input model,
stick packer and Switch 2 battery encoder from the upstream Pro/input modules.
Golden host tests are retained in `tests/switch2_gc/` with renamed input symbols.

Primary references at that revision:

- [Protocol and captures](https://github.com/notsosaelin/PicoSwitch2/blob/e054bfd1311289f392069ff0d2447e01748c2124/docs/switch2-gc/protocol.md)
- [Native mapping and corrected detent bits](https://github.com/notsosaelin/PicoSwitch2/blob/e054bfd1311289f392069ff0d2447e01748c2124/docs/switch2-gc/mapping.md)
- [Reference encoder](https://github.com/notsosaelin/PicoSwitch2/blob/e054bfd1311289f392069ff0d2447e01748c2124/firmware/src/console/gamecube/switch_gc_encode.c)

The executable encoder and hardware-corrected mapping take precedence over the
older button table in protocol.md: report 0x0A payload byte 2 uses 0x10 for
R detent and 0x20 for Z; byte 3 uses 0x10 for L detent and 0x20 for ZL.

Joypad changes:

- A separate `CONFIG_SWITCH2_GC` target, with HID IF0 and a custom bulk IF1
  driver in `../switch2_gc_mode.c`. No reference Bluetooth/input subsystem.
- One outstanding bulk reply. OUT is rearmed after successful IN completion;
  busy, failed queueing and failed completion retain the original reply.
  Commands retain packet boundaries rather than sharing a byte FIFO.
- USB-only dispatch, short-command bounds and EP0 direction/type validation.
- Joypad profile output supplies native controls; detents read only L1/R1.
  Each detent forces its corresponding trigger byte to 255; otherwise trigger
  travel passes through unchanged. The pure encoder still carries separate
  fields. Start+Z supplies ZL (Suspend),
  Start+Up supplies Home, and Start+Down supplies Minus in this mode.
- Shared input model reduced and namespaced; wired unknown battery reports the
  reference external-power default. No motion, microphone, audio or BLE output.
- Session reset clears selected report, counters, pairing key, bulk state and
  rumble. Input retains the latest controller state across console bus resets.
- Upstream stage-history comments were trimmed; remaining capture references in
  copied encoder comments refer to the pinned upstream repository.

Retained protocol decisions: PID 0x2073, reference bcdDevice 0x0111, synthetic
identity/calibration, complete reference command-family replies including USB
AES pairing, report 0x05/0x0A selection, and bounded 40ms binary rumble pulses.
SPI reads retain the upstream 40-byte clamp (a buffer limitation, not a claim
that genuine hardware supports only 40 bytes). Some opaque replies are upstream
stand-ins; console acceptance must be tested on this actual firmware/hardware.

See `docs/SWITCH2_NSO_GAMECUBE.md` for building and hardware validation.
