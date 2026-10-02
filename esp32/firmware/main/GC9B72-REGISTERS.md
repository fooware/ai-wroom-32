# GC9B72 register names and evidence

The driver uses functional names where a GC9B72-specific source identifies the
command's purpose. The extended-command and gamma names below describe the
functions documented by the driver author; they are not claimed to be
GalaxyCore's official register mnemonics.

Source: [guillermozuur-design/gc9b72, INIT_SEQUENCE.md](https://github.com/guillermozuur-design/gc9b72/blob/7b3d6dd1c654ab22e4060409923e75224473f696/INIT_SEQUENCE.md).

| Address | Functional name in this driver | Documented function |
| --- | --- | --- |
| `FE` | `EXTENDED_COMMAND_UNLOCK_1` | Unlock extended commands |
| `EF` | `EXTENDED_COMMAND_UNLOCK_2` | Unlock extended commands |
| `EE` | `EXTENDED_COMMAND_LOCK` | Lock extended commands |
| `F0` | `GAMMA_CONTROL_1` | Gamma control |
| `F1` | `GAMMA_CONTROL_2` | Gamma control |
| `F2` | `GAMMA_CONTROL_3` | Gamma control |
| `F3` | `GAMMA_CONTROL_4` | Gamma control |

The unlock and gamma suffixes distinguish addresses in sequence order. The
source does not identify the gamma commands' polarity, color channel, or payload
bitfields, so the driver does not invent those details. Initialization bytes,
parameter counts, and delays remain unchanged from the attributed xboot source.

Standard commands use ESP-IDF's DCS constants: software reset, sleep out,
display on/off, column/row address windows, memory write, memory access control,
pixel format, inversion, and tearing-effect control.

## Remaining gaps

The other `VENDOR_REG_XX` entries still have unidentified functions. Searches of
GC9B72-specific driver sources, English/Chinese datasheet references, and supplier
materials did not yield a complete controller register map. This is a search
result, not proof that no datasheet exists. These placeholders remain unfinished
semantic documentation.

References checked:

- [xboot/xstar original GC9B72 driver](https://github.com/xboot/xstar/blob/bd51ce0ce0e6350fdb76ccdaed3627d3b8ad84e8/xstar/driver/framebuffer/fb-gc9b72.c): initialization sequence.
- [LovyanGFX GC9B72 profile](https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx/v1/panel/Panel_GC9A01.hpp): same reference sequence, largely numeric vendor commands.
- [MaliosDark Arduino_GC9B72](https://github.com/MaliosDark/Arduino_GC9B72): hardware-tested port of that sequence.
- [TZT supplier module/archive](https://www.tztstore.com/goods/show-8554.html): the linked archive contains an STM32 RGB-interface example with a different initialization sequence, so its vendor names cannot establish the GC9B72 SPI register map.

Do not assign meanings from a GC9A01 or another GalaxyCore controller solely
because an address matches. A GC9B72 datasheet or annotated vendor reference is
needed to finish naming the remaining registers and their parameter fields.
