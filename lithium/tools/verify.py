"""Run the linked MCS-51 policy/protocol/storage tests in uCsim, not a host model."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", default=shutil.which("s51") or "s51")
    args = parser.parse_args()
    build = ROOT / "firmware/build_test"
    image = build / "CC2_LITHIUM_TEST_DO_NOT_FLASH.hex"
    listing = (build / "test_core.rst").read_text(encoding="utf-8")
    stop = int(re.search(r"(?m)^\s+([0-9A-Fa-f]{6})\s+\d+ _test_done:", listing)[1], 16)
    with tempfile.TemporaryDirectory(prefix="cc2_lithium_") as directory:
        script = Path(directory) / "checks.sim"
        script.write_text(
            f"run 0 {stop:#x}\necho ASSERTIONS\nexpression /d xram[0xf000]+256*xram[0xf001]\n"
            "echo FAILURES\nexpression /d xram[0xf002]+256*xram[0xf003]\n"
            "echo FIRST_FAILURE\nexpression /d xram[0xf004]+256*xram[0xf005]\nquit\n",
            encoding="ascii",
        )
        result = subprocess.run(
            [args.simulator, "-q", "-b", "-t", "C52", "-c", str(script), image.name],
            cwd=build, input="", capture_output=True, text=True, timeout=120,
        )
    if result.returncode or "Parser error" in result.stdout:
        raise RuntimeError(result.stdout + result.stderr)
    observed = {key: int(value) for key, value in re.findall(
        r"(?m)^(ASSERTIONS|FAILURES|FIRST_FAILURE)\s*\n\s*(\d+)\s*$", result.stdout)}
    if set(observed) != {"ASSERTIONS", "FAILURES", "FIRST_FAILURE"}:
        raise RuntimeError(result.stdout[-4000:])
    report = {
        "scope": "Linked SDCC policy, CRC/parser, redundant config recovery and legacy odometer/calibration; EEPROM transport mocked",
        "image_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        **observed,
    }
    (build / "verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    print(json.dumps(report, indent=2))
    if observed["FAILURES"] or observed["ASSERTIONS"] < 600:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
