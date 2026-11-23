"""Exercise relay, command and timeout paths in the actual linked firmware image.

UART register reads are injected and transmitted bytes captured. Generic C52 simulation cannot prove XC878 paged
peripherals, clock timing, physical relays, EEPROM programming or watchdog reset.
"""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from bms_protocol import frame as status_frame

ROOT = Path(__file__).resolve().parents[1]


class Simulation:
    def __init__(self, build):
        self.build = build
        self.commands = []
        self.checks = []

    def symbol(self, module, name):
        listing = (self.build / (module + ".rst")).read_text(encoding="utf-8")
        return int(re.search(r"(?m)^\s+([0-9A-Fa-f]{6})\s+\d+ " + re.escape("_" + name) + ":", listing)[1], 16)

    def put(self, space, address, *values):
        self.commands.append(f"set memory {space} {address:#x} " + " ".join(str(x & 255) for x in values))

    def call(self, module, name):
        self.put("iram", 0x21, 0xFE, 0xFF)
        self.put("sfr", 0x81, 0x22)
        self.commands.append(f"run {self.symbol(module, name):#x} 0xfffe")

    def check(self, label, expression, expected, mask=255):
        key = f"CHECK_{len(self.checks)}"
        self.checks.append((key, label, expected, mask))
        self.commands += [f"echo {key}", f"expression /d {expression}"]

    def ram(self, module, name):
        return f"xram[{self.symbol(module, name)}]"

    def now(self, value):
        self.put("xram", self.symbol("timebase", "uptime_ms"), *value.to_bytes(4, "little"))

    def reset(self):
        self.commands += ["reset", "fill iram 0 255 0", "fill xram 0xf600 0xfbff 0", "fill iram 0xf0 0xff 165"]
        self.put("sfr", 0xA8, 0)
        self.put("sfr", 0xB0, 0x10)
        self.put("sfr", 0x80, 0xC0)
        self.put("sfr", 0xC8, 0x10)
        self.put("sfr", 0x92, 1)
        self.call("lithium", "li_init")
        self.call("drive", "drive_init")
        self.call("safety", "safety_reset")

    def line(self, line):
        self.put("xram", 0xFA00, *line.encode("ascii"), 0)
        self.put("xram", 0xFB00, 0)
        self.put("sfr", 0x82, 0, 0xFA)
        self.put("sfr", 0xF0, 0)
        self.call("commands", "commands_handle_line")

    def execute(self, simulator, safe):
        image = self.build / ("CC2_LITHIUM_SAFE.hex" if safe else "CC2_LITHIUM.hex")
        with tempfile.TemporaryDirectory(prefix="cc2_li_integration_") as directory:
            script = Path(directory) / "checks.sim"
            script.write_text("\n".join(self.commands + ["quit"]) + "\n", encoding="ascii")
            result = subprocess.run([simulator, "-q", "-b", "-t", "C52", "-c", str(script), image.name],
                                    cwd=self.build, input="", capture_output=True, text=True, timeout=120)
        if result.returncode or "Parser error" in result.stdout:
            raise RuntimeError(result.stdout + result.stderr)
        values = dict(re.findall(r"(?m)^(CHECK_\d+)\s*\n\s*(\d+)\s*$", result.stdout))
        failures = [{"case": label, "expected": expected, "actual": values.get(key)}
                    for key, label, expected, mask in self.checks
                    if key not in values or int(values[key]) & mask != expected]
        report = {"image_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                  "assertions": len(self.checks), "failures": failures,
                  "scope": __doc__.strip()}
        (self.build / "integration_verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
        print(json.dumps(report, indent=2))
        if failures:
            raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", default=shutil.which("s51") or "s51")
    parser.add_argument("--safe", action="store_true")
    args = parser.parse_args()
    sim = Simulation(ROOT / "firmware" / ("build_safe" if args.safe else "build"))
    # Capture uart_putc(DPL), preserving caller registers. Instrumentation only.
    stub = [0xC0, 0xE0, 0xC0, 0xF0, 0xC0, 0x82, 0xC0, 0x83, 0xC0, 0xD0,
            0x85, 0x82, 0xF0, 0x90, 0xFB, 0, 0xE0, 0xC0, 0xE0, 0x04, 0xF0,
            0x90, 0xFB, 0x20, 0xD0, 0xE0, 0x25, 0x82, 0xF5, 0x82,
            0xE4, 0x35, 0x83, 0xF5, 0x83, 0xE5, 0xF0, 0xF0,
            0xD0, 0xD0, 0xD0, 0x83, 0xD0, 0x82, 0xD0, 0xF0, 0xD0, 0xE0, 0x22]
    sim.put("rom", sim.symbol("uart", "uart_putc"), 0x02, 0xF0, 0)
    sim.put("rom", 0xF000, *stub)
    for vector, module, handler in ((0x0B, "timebase", "timebase_timer0_isr"),
                                    (0x23, "uart", "uart_isr"),
                                    (0x3B, "vehicle_eeprom", "vehicle_eeprom_ssc_isr"),
                                    (0x4B, "speed", "speed_external_isr")):
        address = sim.symbol(module, handler)
        for offset, value in enumerate((2, address >> 8, address & 255)):
            sim.check(f"interrupt vector {vector:02X} byte {offset}", f"rom[{vector + offset}]", value)
    sim.reset()
    sim.now(0)
    sim.call("controller", "controller_tick")
    sim.check("boot charge inactive", "sfr[176]", 16, 16)
    sim.check("boot no motor", "sfr[176]", 0, 32)
    sim.check("boot no foot", "sfr[128]", 0, 16)
    sim.put("sfr", 0xC8, 0x11)
    sim.now(10); sim.call("controller", "controller_tick")
    sim.now(1010); sim.call("controller", "controller_tick")
    sim.check("default external charge output", "sfr[176]", 16 if args.safe else 0, 16)
    sim.check("never use full/balance relay", "sfr[176]", 0, 64)
    sim.put("sfr", 0xC8, 0x01)
    sim.call("controller", "controller_apply_outputs")
    sim.check("raw ignition immediately blocks charge", "sfr[176]", 16, 16)
    sim.put("sfr", 0xC8, 0x10)
    sim.call("controller", "controller_apply_outputs")
    sim.check("raw unplug blocks charge", "sfr[176]", 16, 16)
    sim.put("xram", sim.symbol("safety", "safety_lease_ms"), 3, 0)
    sim.put("sfr", 0xB0, 0x60)
    sim.put("sfr", 0x80, 0x18)
    sim.put("sfr", 0xB2, 2)
    for _ in range(3): sim.call("timebase", "timebase_timer0_isr")
    sim.check("timer expiry deenergizes charge/motor/full", "sfr[176]", 16, 112)
    sim.check("timer expiry drops foot, preserves direction", "sfr[128]", 8, 24)
    sim.check("timer preserves port page", "sfr[178]", 2)
    sim.check("timer expiry latched", sim.ram("safety", "safety_expired"), 1)
    sim.call("controller", "controller_tick")
    sim.check("foreground records timeout fault", sim.ram("lithium", "li_faults"), 64)
    sim.line("FAULT,CLEAR")
    sim.check("explicit stopped clear succeeds", sim.ram("safety", "safety_expired"), 0)
    sim.check("fault latch clears", sim.ram("lithium", "li_faults"), 0)
    sim.reset()
    sim.line("CONFIG,LIMITS,24,2500,3650,0,450,-200,600,200,1000,3600")
    sim.check("limits command cells", f"xram[{sim.symbol('lithium', 'li_config') + 6}]", 24)
    sim.check("limits command retained mode", sim.ram("lithium", "li_config"), 1)
    sim.line("CHARGE,SUPERVISED")
    sim.check("supervised mode selectable", sim.ram("lithium", "li_config"), 2)
    body = "BMS,1,1,19,24,7920,3290,3310,0,200,250,100,500"
    frame = body + f"*{binascii.crc_hqx(body.encode(), 0xFFFF):04X}"
    sim.line(frame)
    sim.check("framed command updates sequence", sim.ram("lithium", "li_packet"), 1)
    response = status_frame("CC,1,1,0,2,1,0,0,0,0,0,0,500,0,0") + "\r\n"
    sim.check("status byte count", "xram[64256]", len(response))
    for index, value in enumerate(response.encode("ascii")):
        sim.check(f"status byte {index}", f"xram[{0xFB20 + index}]", value)
    sim.check("command parser stack tail intact", "iram[240]+iram[255]", 330, 65535)
    sim.line("STOP")
    sim.check("STOP inhibits charge mode", sim.ram("lithium", "li_config"), 0)
    sim.check("STOP inhibits drive mode", f"xram[{sim.symbol('lithium', 'li_config') + 1}]", 0)
    sim.line("DRIVE,EXTERNAL")
    sim.put("sfr", 0xC8, 0)
    sim.now(10)
    for _ in range(55): sim.call("controller", "controller_tick")
    sim.check("drive motor sequence", "sfr[176]", 0 if args.safe else 32, 32)
    for _ in range(10): sim.call("controller", "controller_tick")
    sim.check("drive foot sequence", "sfr[128]", 0 if args.safe else 16, 16)
    sim.put("sfr", 0xC8, 1)
    sim.call("controller", "controller_tick")
    sim.check("raw charger inhibits motor before debounce", "sfr[176]", 0, 32)
    sim.check("raw charger inhibits foot before debounce", "sfr[128]", 0, 16)
    sim.reset()
    sim.call("uart", "uart_init")
    # C52 SBUF writes feed its TX register, not RX. Substitute only that read.
    uart_listing = (sim.build / "uart.rst").read_text(encoding="utf-8")
    rx_read = int(re.search(r"(?m)^\s+([0-9A-F]{6}) AF 99\s+.*mov\s+r7,_SBUF", uart_listing)[1], 16)
    for byte in b"$PING#":
        sim.put("rom", rx_read, 0x7F, byte)
        sim.put("sfr", 0x98, 0x71)
        sim.call("uart", "uart_isr")
    sim.put("xram", 0xFB00, 0)
    sim.call("commands", "commands_poll")
    for index, value in enumerate(b"$PONG#\r\n"):
        sim.check(f"interrupt RX command byte {index}", f"xram[{0xFB20 + index}]", value)
    for _ in range(129):
        sim.put("rom", rx_read, 0x7F, 65)
        sim.put("sfr", 0x98, 0x71)
        sim.call("uart", "uart_isr")
    sim.call("commands", "commands_poll")
    sim.check("RX overflow fault even in external mode", sim.ram("lithium", "li_faults"), 128)
    sim.check("RX overflow inhibits motor", "sfr[176]", 0, 32)
    sim.check("RX overflow inhibits charge", "sfr[176]", 16, 16)
    sim.execute(args.simulator, args.safe)


if __name__ == "__main__":
    main()
