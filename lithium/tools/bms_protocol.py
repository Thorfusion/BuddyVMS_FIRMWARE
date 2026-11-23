"""PoldenTEK CC2 universal BMS adapter protocol v1 codec and bench encoder."""
import argparse
import binascii
from dataclasses import dataclass
import json

VALID = 16
CHARGE = 1
DRIVE = 2
COMPLETE = 4
FAULT = 8


def frame(body: str) -> str:
    raw = body.encode("ascii")
    if any(x in body for x in "$#*\r\n"):
        raise ValueError("invalid framing character")
    return f"${body}*{binascii.crc_hqx(raw, 0xFFFF):04X}#"


@dataclass(frozen=True)
class BmsStatus:
    sequence: int
    flags: int
    cells: int
    pack_cv: int
    cell_min_mv: int
    cell_max_mv: int
    temp_min_dc: int
    temp_max_dc: int
    current_da: int
    soc10: int

    def encode(self) -> str:
        unsigned = (self.sequence, self.flags, self.cells, self.pack_cv,
                    self.cell_min_mv, self.cell_max_mv, self.soc10)
        if any(not isinstance(x, int) or not 0 <= x <= 65535 for x in unsigned):
            raise ValueError("unsigned field out of range")
        if not self.flags & VALID or self.flags & ~31:
            raise ValueError("measurements must be valid; unknown flags forbidden")
        if not 1 <= self.cells <= 96 or not 500 <= self.cell_min_mv <= self.cell_max_mv <= 5000:
            raise ValueError("invalid cell measurements")
        if not -500 <= self.temp_min_dc <= self.temp_max_dc <= 1250:
            raise ValueError("invalid temperature measurements")
        if not -32768 <= self.current_da <= 32767 or not (0 <= self.soc10 <= 1000 or self.soc10 == 65535):
            raise ValueError("current/SOC out of range")
        if not self.cell_min_mv * self.cells - 20 <= self.pack_cv * 10 <= self.cell_max_mv * self.cells + 20:
            raise ValueError("pack voltage inconsistent with cell extrema")
        values = (1, self.sequence, self.flags, self.cells, self.pack_cv,
                  self.cell_min_mv, self.cell_max_mv, 0, self.temp_min_dc,
                  self.temp_max_dc, self.current_da, self.soc10)
        return frame("BMS," + ",".join(str(x) for x in values))


CC_FIELDS = ("sequence", "uptime_ms", "charge_mode", "drive_mode", "charge_state",
             "charge_reason", "faults", "drive_state", "outputs", "speed_kmh",
             "soc10", "bms_age_ms", "drive_reason")


def decode_cc(text: str) -> dict:
    text = text.rstrip("\r\n")
    if not text.startswith("$") or not text.endswith("#") or text.count("*") != 1:
        raise ValueError("invalid frame")
    body, checksum = text[1:-1].split("*")
    if len(checksum) != 4 or any(c not in "0123456789ABCDEF" for c in checksum):
        raise ValueError("invalid checksum field")
    if binascii.crc_hqx(body.encode("ascii"), 0xFFFF) != int(checksum, 16):
        raise ValueError("CRC mismatch")
    values = body.split(",")
    if values[:2] != ["CC", "1"] or len(values) != 15:
        raise ValueError("unsupported status schema")
    if any(not x.isascii() or not x.isdigit() for x in values[2:]):
        raise ValueError("invalid number")
    result = dict(zip(CC_FIELDS, map(int, values[2:])))
    if any(x > 0xFFFFFFFF for x in result.values()):
        raise ValueError("field overflow")
    result["soc_percent"] = None if result["soc10"] == 65535 else result["soc10"] / 10
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decode")
    parser.add_argument("--sequence", type=int, default=1)
    parser.add_argument("--flags", type=int, default=VALID)
    for name in ("cells", "pack-cv", "cell-min-mv", "cell-max-mv",
                 "temp-min-dc", "temp-max-dc", "current-da", "soc10"):
        parser.add_argument("--" + name, type=int)
    args = parser.parse_args()
    if args.decode:
        print(json.dumps(decode_cc(args.decode), indent=2))
    else:
        values = {key: value for key, value in vars(args).items() if key != "decode"}
        if any(value is None for value in values.values()):
            parser.error("all measurement fields are required when encoding")
        print(BmsStatus(**values).encode())


if __name__ == "__main__":
    main()
