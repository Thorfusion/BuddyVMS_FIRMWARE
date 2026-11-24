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

ROOT = Path(__file__).resolve().parents[1]


class Simulation:
    def __init__(self, build):
        self.build = build
        self.commands = []
        self.checks = []
        self.captures = []

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
        self.call("speed", "speed_init")
        self.call("drive", "drive_init")
        self.call("safety", "safety_reset")
        self.call("reports", "reports_init")

    def drain(self, polls=100):
        for _ in range(polls): self.call("reports", "reports_poll")

    def line(self, line, polls=100, reset_capture=True):
        self.put("xram", 0xB100, *line.encode("ascii"), 0)
        if reset_capture: self.put("xram", 0xB000, 0, 0)
        self.put("sfr", 0x82, 0, 0xB1)
        self.put("sfr", 0xF0, 0)
        self.call("commands", "commands_handle_line")
        self.drain(polls)

    def capture(self, label):
        self.captures.append(label)
        self.commands += [f"echo CAPTURE_{label}", "expression /d xram[45056]+256*xram[45057]",
                          "dump /h xram 0xc000 0xcfff 16", f"echo END_{label}"]

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
        transcripts = {}
        for label in self.captures:
            block = re.search(r"(?ms)^CAPTURE_" + label + r"\s*\n(.*?)^END_" + label + r"\s*$", result.stdout)
            if not block: raise RuntimeError(f"Missing capture {label}")
            lines = block[1].strip().splitlines()
            count = int(lines[0])
            memory = {}
            for line in lines[1:]:
                parts = line.split()
                if parts and re.fullmatch(r"0x[0-9a-fA-F]+", parts[0]):
                    for offset, value in enumerate(parts[1:17]):
                        memory[int(parts[0], 16) + offset] = int(value, 16)
            if count > 4096: raise RuntimeError("Capture overflow")
            transcripts[label] = bytes(memory[0xC000 + i] for i in range(count)).decode("ascii")
        (self.build / "report_transcripts.json").write_text(json.dumps(transcripts, indent=2) + "\n", encoding="ascii")
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
    # uart_try_putc(DPL): capture bytes at C000 with B000 LE16 count; return ready.
    stub = [0xC0, 0xE0, 0xC0, 0xF0, 0xC0, 0x82, 0xC0, 0x83, 0xC0, 0xD0, 0xC0, 0, 0xC0, 1,
            0x85, 0x82, 0xF0, 0x90, 0xB0, 0, 0xE0, 0xF8, 0xA3, 0xE0, 0xF9,
            0x88, 0x82, 0xE9, 0x24, 0xC0, 0xF5, 0x83, 0xE5, 0xF0, 0xF0,
            0x08, 0xE8, 0x70, 1, 0x09, 0x90, 0xB0, 0, 0xE8, 0xF0, 0xA3, 0xE9, 0xF0,
            0xD0, 1, 0xD0, 0, 0xD0, 0xD0, 0xD0, 0x83, 0xD0, 0x82, 0xD0, 0xF0,
            0xD0, 0xE0, 0x75, 0x82, 1, 0x22]
    sim.put("rom", sim.symbol("uart", "uart_try_putc"), 0x02, 0xF0, 0)
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
    response = "@B,1,129\n@D,1,1,0,0,0,0000,0000\n@D,1,2,0,0,1,0,0,0,1,1,0,0\n@D,1,3,50\n@E,1,3\n"
    sim.check("status byte count", "xram[45056]", len(response))
    for index, value in enumerate(response.encode("ascii")):
        sim.check(f"status byte {index}", f"xram[{0xC000 + index}]", value)
    sim.capture("bms_status")
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
    rx_match = re.search(r"(?m)^\s+([0-9A-F]{6}) A[8-F] 99\s+.*mov\s+r([0-7]),_SBUF", uart_listing)
    if not rx_match:
        raise RuntimeError("Cannot locate the UART receive-register instruction")
    rx_read = int(rx_match[1], 16)
    rx_opcode = 0x78 + int(rx_match[2])

    def receive(data):
        for byte in data:
            sim.put("rom", rx_read, rx_opcode, byte)
            sim.put("sfr", 0x98, 0x71)
            sim.call("uart", "uart_isr")

    receive(b"$PING#")
    sim.put("xram", 0xB000, 0, 0)
    sim.call("commands", "commands_poll")
    sim.drain()
    for index, value in enumerate(b"@OK,PING,0\n"):
        sim.check(f"interrupt RX command byte {index}", f"xram[{0xC000 + index}]", value)
    receive(b"A" * 129)
    sim.call("commands", "commands_poll")
    sim.check("RX overflow fault even in external mode", sim.ram("lithium", "li_faults"), 128)
    sim.check("RX overflow inhibits motor", "sfr[176]", 0, 32)
    sim.check("RX overflow inhibits charge", "sfr[176]", 16, 16)
    receive(b"FAULT,CLEAR\n")
    sim.call("commands", "commands_poll")
    sim.check("RX overflow tail cannot clear its own fault", sim.ram("lithium", "li_faults"), 128)
    receive(b"FAULT,CLEAR\n")
    sim.call("commands", "commands_poll")
    sim.check("fresh native command after LF may clear the fault", sim.ram("lithium", "li_faults"), 0)
    sim.drain()
    for delimiter in (b"#", b"$"):
        sim.reset(); sim.call("uart", "uart_init")
        receive(b"A" * 129)
        sim.call("commands", "commands_poll")
        receive(b"CHARGE,OFF" + delimiter)
        sim.call("commands", "commands_poll")
        sim.check("RX overflow suffix cannot change charge policy", sim.ram("lithium", "li_config"), 1)
        sim.check("RX recovery delimiter does not clear UART fault", sim.ram("lithium", "li_faults"), 128)
        sim.put("xram", 0xB000, 0, 0)
        receive(b"PING#" if delimiter == b"$" else b"PING\n")
        sim.call("commands", "commands_poll"); sim.drain()
        sim.capture("rx_recovery_dollar" if delimiter == b"$" else "rx_recovery_hash")
    sim.reset()
    io_stub = [0xC0, 0xE0, 0xC0, 0x82, 0xC0, 0x83, 0x90, 0xB0, 0x20,
               0xE0, 0x04, 0xF0, 0xD0, 0x83, 0xD0, 0x82, 0xD0, 0xE0, 0x75, 0x82, 1, 0x22]
    sim.put("rom", 0xF100, *io_stub)
    for module, name in (("vehicle_eeprom", "vehicle_eeprom_read"),
                         ("vehicle_eeprom", "vehicle_eeprom_write_verified"),
                         ("display_eeprom", "display_eeprom_read"),
                         ("display_eeprom", "display_eeprom_write_verified")):
        sim.put("rom", sim.symbol(module, name), 2, 0xF1, 0)
    guarded = ("BMS,RESET", "CONFIG,SAVE", "CONFIG,DEFAULTS", "CONFIG,FAN,410,350",
               "CONFIG,LIMITS,24,2500,3650,0,450,-200,600,200,1000,3600",
               "SPEEDCAL,1620", "EEPROM,READ,VMS,0,16", "CHARGE,FORCE", "DRIVE,EXTERNAL")
    for cause in ("charger", "ignition", "motion", "drive_outputs"):
        sim.reset()
        sim.put("xram", 0xB020, 0)
        sim.put("xram", 0xB000, 0, 0)
        if cause == "charger": sim.put("sfr", 0xC8, 0x11)
        elif cause == "ignition": sim.put("sfr", 0xC8, 0)
        elif cause == "motion": sim.put("xram", sim.symbol("speed", "pulse_counter"), 1, 0)
        else: sim.put("sfr", 0xB0, 0x30)
        for command in guarded: sim.line(command, polls=12, reset_capture=False)
        sim.capture("interlocks_" + cause)
        sim.check("blocked service commands perform no EEPROM transactions: " + cause, "xram[45088]", 0)
        sim.check("blocked defaults/modes preserve charge policy: " + cause, sim.ram("lithium", "li_config"), 1)
        sim.check("blocked limits do not change cell count: " + cause, f"xram[{sim.symbol('lithium', 'li_config') + 6}]", 0)
    sim.reset()
    sim.put("xram", 0xB020, 0)
    sim.put("xram", 0xB000, 0, 0)
    for command in ("CONFIG,FAN,invalid,350", "CONFIG,FAN,350,400", "SPEEDCAL,invalid",
                    "SPEEDCAL,99", "EEPROM,READ,VMS,-1,16", "EEPROM,READ,VMS,0,17", "CONFIG,UNKNOWN"):
        sim.line(command, polls=12, reset_capture=False)
    sim.capture("service_parameter_errors")
    sim.check("invalid service parameters perform no EEPROM transactions", "xram[45088]", 0)
    sim.reset()
    sim.put("xram", 0xB020, 0)
    sim.line("IDENTIFY"); sim.capture("identify")
    sim.line("ID"); sim.capture("native_id")
    sim.line("REPORT", polls=700); sim.capture("empty_report")
    sim.line("CONFIG,LIMITS,24,2500,3650,0,450,-200,600,200,1000,3600")
    sim.line(frame)
    sim.line("REPORT", polls=700); sim.capture("valid_report")
    sim.now(1500)
    sim.line("REPORT", polls=700); sim.capture("stale_report")
    sim.line("LOG"); sim.capture("unsupported_log")
    sim.line("READJOURNAL"); sim.capture("unsupported_journal")
    sim.line("BMSREPORT"); sim.capture("unsupported_bmsreport")
    sim.check("reports and identity perform no EEPROM transactions", "xram[45088]", 0)
    sim.check("read-only reports do not alter charge mode", sim.ram("lithium", "li_config"), 1)
    sim.check("read-only reports do not alter fault latch", sim.ram("lithium", "li_faults"), 0)
    sim.put("xram", sim.symbol("speed", "odometer_tenths"), 255, 255, 255, 255)
    sim.put("xram", sim.symbol("legacy", "committed"), *int(1234567).to_bytes(4, "little"))
    sim.put("xram", sim.symbol("legacy", "odo_source"), 2)
    sim.line("LIFETIME"); sim.capture("legacy_lifetime")
    sim.put("xram", sim.symbol("lithium", "event_total"), 251, 255)
    for tick in range(12):
        sim.put("xram", sim.symbol("lithium", "li_faults"), tick & 1, 0)
        sim.now(1600 + tick * 10); sim.call("controller", "controller_tick")
    sim.line("EVENTS", polls=250); sim.capture("wrapping_events")
    # Native LF and CRLF parsing use the real command buffer and RX ring.
    sim.reset(); sim.call("uart", "uart_init")
    for message, label in ((b"IDENTIFY\n", "lf_identify"), (b"ID\n", "lf_id"),
                           (b"ID\r\n", "crlf_id"), (b"$ID#", "legacy_id"),
                           (b"$IDENTIFY#", "wrapped_identify"), (b"VERSION\r\n", "crlf_version"),
                           (b"A" * 150 + b"STOP\nPING\n", "overflow_recovery")):
        sim.put("xram", 0xB000, 0, 0)
        for index, byte in enumerate(message):
            sim.put("rom", rx_read, rx_opcode, byte)
            sim.put("sfr", 0x98, 0x71)
            sim.call("uart", "uart_isr")
            if index % 16 == 15: sim.call("commands", "commands_poll"); sim.drain(20)
        for _ in range(3): sim.call("commands", "commands_poll"); sim.drain()
        sim.capture(label)
    sim.check("overflow suffix cannot execute STOP", sim.ram("lithium", "li_config"), 1)
    # A busy report accepts fresh BMS input and permits an immediate STOP.
    sim.reset()
    sim.line("REPORT", polls=1)
    body2 = body.replace(",1,19,", ",2,19,")
    sim.line(body2 + f"*{binascii.crc_hqx(body2.encode(), 0xFFFF):04X}", polls=0, reset_capture=False)
    sim.check("BMS still accepted during aggregate report", sim.ram("lithium", "li_packet"), 2)
    sim.line("STOP", polls=0, reset_capture=False)
    sim.check("STOP immediate while report remains pending", sim.ram("lithium", "li_config"), 0)
    sim.line("STATUS", polls=0, reset_capture=False)
    sim.drain(700)
    sim.capture("interleaved_aggregate")
    # Restore just the instrumented TX entry and make the actual driver busy.
    rom = {}
    for record in (sim.build / "CC2_LITHIUM.hex" if not args.safe else sim.build / "CC2_LITHIUM_SAFE.hex").read_text().splitlines():
        raw = bytes.fromhex(record[1:])
        if raw[3] == 0:
            address = int.from_bytes(raw[1:3], "big")
            rom.update((address + i, value) for i, value in enumerate(raw[4:-1]))
    tx = sim.symbol("uart", "uart_try_putc")
    sim.put("rom", tx, *[rom[tx + i] for i in range(3)])
    sim.reset()
    sim.put("xram", sim.symbol("uart", "tx_done"), 0)
    sim.line("REPORT", polls=0)
    for tick in range(1, 21):
        sim.now(tick * 10)
        sim.call("controller", "controller_tick")
        sim.call("reports", "reports_poll")
    sim.check("blocked TX does not block foreground control", sim.ram("lithium", "li_faults"), 0)
    sim.call("reports", "reports_busy")
    sim.check("blocked TX retains pending report", "sfr[130]", 1)
    sim.now(1200); sim.call("reports", "reports_poll")
    sim.check("TX timeout aborts instead of completing report", sim.ram("reports", "request"), 0)
    sim.put("rom", tx, 2, 0xF0, 0)
    sim.call("reports", "reports_poll")
    sim.line("IDENTIFY"); sim.capture("after_tx_abort_identify")
    sim.execute(args.simulator, args.safe)


if __name__ == "__main__":
    main()
