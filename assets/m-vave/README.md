# Official FM-1 V15 firmware (M-VAVE, Apache-2.0)

`FM-1_015.fwsc` is M-VAVE's official FM-1 firmware V15, unmodified. SWITCH_RESET doesn't install it: the build
(`tools/usr_data.py`) takes V15's factory USR data (its voices and settings, 73,226 bytes) out of it, checks it and
puts it in the firmware, which writes it at 0xEA000.

| | |
| --- | --- |
| Source | <https://github.com/m-vave/fm1_open>, `sdk/firmware/FM-1_015.fwsc`, commit `3be7fbdbd3bb137bacbd65d68412c210eb977870` |
| Also | <https://www.m-vave.com/download> (PC Firmware / FM-1 V15), the same file |
| Size | 699956 bytes |
| SHA-256 | `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` |
| Licence | Apache License, Version 2.0 (`LICENSES/Apache-2.0.txt`) |

M-VAVE's terms for it, from `THIRD_PARTY.md` in that repository: the recovery image `sdk/firmware/FM-1_015.fwsc` is
distributed by M-VAVE (珠海笙科智能科技有限公司) under the Apache License, Version 2.0, which permits use, modification and
redistribution, including binary redistribution, subject to its terms. M-VAVE supply no NOTICE file for it.
