# GC9B72 register names and evidence

The initialization bytes come from the GC9B72 xboot driver. Semantic names for
matching vendor addresses are derived from the sibling **GC9B71 datasheet v1.1**.
This is an inference about GC9B72 functions, not a claim that its register map or
parameter bitfields are identical. The known differences are retained below.

## Sources

- **xboot:** [`fb-gc9b72.c`, revision bd51ce0ce0e6350fdb76ccdaed3627d3b8ad84e8](https://github.com/xboot/xstar/blob/bd51ce0ce0e6350fdb76ccdaed3627d3b8ad84e8/xstar/driver/framebuffer/fb-gc9b72.c). Establishes the GC9B72 command order, parameter bytes and delays; most vendor writes have no semantic labels.
- **GC9B71 spec sheet:** GalaxyCore *GC9B71 DataSheet*, version 1.1, 236 pages, supplied as `GC_9_B71_Data_Sheet_V1_1_622df4ec27.pdf`. Page numbers below are the printed pages (also the PDF page numbers). SHA-256: `6a5891d9fb92a795d071cf79a58e304993d935efdb14a2b7a04186810f7de460`.

## Derived command comparison

Parameter counts are **bytes following the command byte**, excluding the command
itself. Both source columns include their own count so differences remain visible.
Vendor constant names in the code have the `GC9B72_` prefix; standard commands use
ESP-IDF's existing `LCD_CMD_*` constants.

| Derived command / code name | Hex command | xboot GC9B72 initialization | GC9B71 spec sheet |
| --- | --- | --- | --- |
| Sleep Out / `LCD_CMD_SLPOUT` | `0x11` | **0 parameters**; wait 120 ms | **0 parameters**; Sleep Out Mode, §5.2.4, p. 148 |
| Display On / `LCD_CMD_DISPON` | `0x29` | **0 parameters**; wait 20 ms | **0 parameters**; Display ON, §5.2.10, p. 155 |
| Tearing Effect Line On / `LCD_CMD_TEON` | `0x35` | **1 parameter**: `00` | **1 parameter**: TE mode; §5.2.17, p. 170 |
| Memory Access Control / `LCD_CMD_MADCTL` | `0x36` | **1 parameter**: `00` (our driver substitutes configured RGB/BGR/orientation) | **1 parameter**: memory access/orientation bits; §5.2.18, pp. 172–174 |
| Pixel Format Set / `LCD_CMD_COLMOD` | `0x3A` | **1 parameter**: `05` | **1 parameter**: low `DBI[2:0]=101` means 16-bit pixels; the table depicts upper bits `01100`, differing from xboot; §5.2.22, p. 181 |
| Tearing Effect Width Control / `TEARING_EFFECT_WIDTH_CONTROL` | `0xB4` | **1 parameter**: `0A` | **2 parameters**: pulse width, then polarity; §5.4, p. 202. **Count differs.** |
| Power Control 2 / `POWER_CONTROL_2` | `0xC3` | **1 parameter**: `1A` | **1 parameter**: `vreg1_vbp_d[6:0]`, positive grayscale reference adjustment; §5.5.4, p. 207. `1A` is within the documented range. |
| Power Control 3 / `POWER_CONTROL_3` | `0xC4` | **1 parameter**: `24` | **1 parameter**: `vreg1_vbn_d[6:0]`, negative grayscale reference adjustment; §5.5.5, p. 208. `24` is within the documented range. |
| Power Control 4 / `POWER_CONTROL_4` | `0xC9` | **1 parameter**: `2F` | **1 parameter**: `vrh[5:0]`, grayscale reference adjustment affecting positive/negative outputs; §5.5.6, p. 209. `2F` fits the field. |
| Inversion / `INVERSION_CONTROL` | `0xEC` | **1 parameter**: `07` | **1 parameter** in §5.5.1, p. 204: `DINV[2:0]` occupies bits **6:4**. Mode 7 would encode as `70`, not `07`. The summary on p. 140 additionally lists a second `RTN2` byte absent from the detailed definition. **The sheet is internally inconsistent here.** |
| Inter Register Enable 1 / `INTER_REGISTER_ENABLE_1` | `0xFE` | **0 parameters**; immediately followed by `EF` at startup | **0 parameters**; first command of the enable sequence; §5.5.7, p. 210 |
| Inter Register Enable 2 / `INTER_REGISTER_ENABLE_2` | `0xEF` | **0 parameters**; follows `FE` | **0 parameters**; second command of the enable sequence; §5.5.8, p. 211 |
| SET_GAMMA1 / `SET_GAMMA1` | `0xF0` | **6 parameters**: `11 17 08 06 05 38` | **6 parameters**: first negative-polarity gamma block, covering VR0/1/2/4/6/13/20 and gradient fields; §5.5.9, pp. 212–213 |
| SET_GAMMA2 / `SET_GAMMA2` | `0xF1` | **6 parameters**: `4D 72 72 2D 34 8F` | **6 parameters**: second negative-polarity gamma block, covering VR27/36/43/50/57/59/61/62/63; §5.5.10, pp. 214–215 |
| SET_GAMMA3 / `SET_GAMMA3` | `0xF2` | **6 parameters**: `11 17 08 06 05 38` | **6 parameters**: first positive-polarity gamma block, corresponding to SET_GAMMA1; §5.5.11, pp. 216–217 |
| SET_GAMMA4 / `SET_GAMMA4` | `0xF3` | **6 parameters**: `4D 72 72 2D 34 8F` | **6 parameters**: second positive-polarity gamma block, corresponding to SET_GAMMA2; §5.5.12, pp. 218–219 |

The paired gamma writes use identical payloads for negative and positive polarity.
Their six-byte layouts fit the GC9B71 field widths. The power-control values fit
GC9B71's documented ranges; they are panel tuning values, not defaults to replace.

## Unresolved EE command

| Command / code name | Hex command | xboot GC9B72 initialization | GC9B71 spec sheet |
| --- | --- | --- | --- |
| Unidentified vendor command / `VENDOR_REG_EE` | `0xEE` | **0 parameters**; follows `FE` near the end of initialization | **Not listed**; parameter count/function unknown. Pages 210–211 say only hardware/software reset clears the internal-command enable state. |

A [GC9B72 driver author's init documentation](https://github.com/guillermozuur-design/gc9b72/blob/7b3d6dd1c654ab22e4060409923e75224473f696/INIT_SEQUENCE.md)
labels EE as an extended-command lock. The GC9B71 sheet does not corroborate that
interpretation, so the code no longer presents it as an established meaning.

## Remaining gaps and implementation policy

The other `VENDOR_REG_XX` commands remain unidentified in these sources. The
GC9B71 sheet documents 16 of the 60 distinct initialization command addresses;
shared addresses alone do not prove their GC9B72 payload layouts.

All xboot initialization bytes, counts and delays are preserved. In particular,
the driver does not add a second B4 parameter, move EC's `07` to `70`, or change
COLMOD's `05` based on a sibling chip's datasheet. Those changes require GC9B72
specific evidence and panel testing. The inversion register here controls panel
drive polarity; it is separate from the standard INVON/INVOFF commands.
