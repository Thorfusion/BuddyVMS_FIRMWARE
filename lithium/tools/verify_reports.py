"""Validate compiled transcripts with the existing BuddySoftware JSON validator/parser.

This imports only the pure protocol module in a separate process. No serial
session, UI, automatic service action, EEPROM access or flashing is performed.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--safe", action="store_true")
    parser.add_argument("--desktop-source", type=Path,
                        default=ROOT.parents[1] / "BuddySoftware")
    args = parser.parse_args()
    source = args.desktop_source / "buddy_software/protocols.py"
    if not source.is_file():
        raise SystemExit("BuddySoftware protocol module required; pass --desktop-source PATH")
    spec = importlib.util.spec_from_file_location("cc2_desktop_contract", source)
    desktop = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = desktop
    spec.loader.exec_module(desktop)
    raw = (ROOT / "protocol/protocol.json").read_bytes()
    protocol = json.loads(raw)
    info = desktop.load_da64_protocol_definition(raw.decode("ascii"), expected_schema=1)
    profile = protocol["cc2_profile"]
    build = ROOT / "firmware" / ("build_safe" if args.safe else "build")
    build_info = json.loads((build / "build_info.json").read_text())
    transcripts = json.loads((build / "report_transcripts.json").read_text())
    checks = 0

    def check(condition, label):
        nonlocal checks
        checks += 1
        if not condition: raise AssertionError(label)

    def parse(label):
        instance = desktop.Da64SerialReportParser()
        reports = []
        for line in transcripts[label].splitlines():
            item = instance.feed_line(line)
            check(not isinstance(item, desktop.Da64ProtocolError), f"{label}: {item}")
            if isinstance(item, desktop.Da64Report):
                reports.append(item)
                # Exercise actual desktop presentation, not just the envelope.
                desktop.da64_report_rows(item)
        return {report.report_id: report for report in reports}, reports

    check(info.report_count == 10, "exact implemented report count")
    check(profile["id"] == "CC2_LITHIUM" and profile["mcu"] == "XC878" and
          profile["chemistry"] == "LITHIUM", "identity profile")
    check(profile["service_baud"] == 19200, "do not inherit DA64 baud")
    check(not profile["legacy_log"] and not profile["bms_bridge"], "no fake LOG/INA229 bridge")
    check(len(profile["aggregate_reports"]) == len(set(profile["aggregate_reports"])), "unique aggregate IDs")
    for report, version in profile["supported_reports"].items():
        check(version in protocol["reports"][report]["schema_versions"], "supported pair exists")
    digest = hashlib.sha256(raw).hexdigest()
    expected_identity = ("@OK,IDENTIFY,1,CC2_LITHIUM,XC878,LITHIUM," +
                         build_info["build_id"] + "," + digest + "\n")
    check(transcripts["identify"] == expected_identity, "identity binds exact protocol/build")
    check(transcripts["lf_identify"] == expected_identity, "native LF IDENTIFY")
    check(transcripts["after_tx_abort_identify"] == expected_identity, "recovery after abandoned TX")
    check(transcripts["crlf_version"] == "@B,2,1\n@D,2,1,3,1,0\n@E,2,1\n", "CRLF native Version")
    for label in ("native_id", "lf_id", "crlf_id", "wrapped_identify"):
        check(transcripts[label] == expected_identity, "ID and IDENTIFY aliases: " + label)
    version = build_info['version']
    check(version == build_info['build_id'], 'firmware version is the release build ID')
    check(transcripts["legacy_id"] == f"$ID,BUDDY-CC2-LITHIUM,{version}#\n", "explicit ID compatibility")
    check(build_info["protocol_sha256"] == digest, "build digest")

    for label in ("empty_report", "valid_report", "stale_report"):
        by_id, ordered = parse(label)
        check([r.report_id for r in ordered] == profile["aggregate_reports"], f"{label}: aggregate order")
        for report in ordered:
            check(report.schema_version == profile["supported_reports"][str(report.report_id)], "profile schema binding")
        check(not any(key in by_id for key in (5, 10, 11, 12, 14, 16, 17, 18)), "no unsupported hardware/history")
        relay_rows = desktop.da64_report_rows(by_id[8])
        check(any("Unavailable (no valid inputs)" in str(row) for row in relay_rows), "unknown relay feedback not Off")

    empty, _ = parse("empty_report")
    stale, _ = parse("stale_report")
    valid, _ = parse("valid_report")
    for reports in (empty, stale):
        check([r.field_id for r in reports[3].records] == [1], "no fabricated stale temperature")
        check([r.field_id for r in reports[4].records] == [1], "no fabricated stale electrical sample")
        check(not any(r.field_id == 3 for r in reports[1].records), "unknown SOC is absent, not 255 percent")
    check(valid[3].records[1].values == ("200", "250"), "pack extrema not invented front/rear/box")
    check(valid[4].records[1].values == ("7920", "3290", "3310", "24", "100", "0013"), "measurement units")
    check(valid[4].records[2].values == ("5000",), "SOC preserves tenths via centipercent")
    check(desktop.format_da64_report_value("current_dA", "100") == "10.0 A", "existing current conversion")
    check(desktop.format_da64_report_value("pack_cV", "7920") == "79.20 V", "existing voltage conversion")
    check(desktop.format_da64_report_value("charge_state", "3") == "Charge requested", "lithium enum not DA64 enum")
    check(desktop.format_da64_report_value("product_id", "3").endswith("(XC878)"), "distinct product ID")
    for label, command in (("unsupported_log", "LOG"), ("unsupported_journal", "READJOURNAL"),
                           ("unsupported_bmsreport", "BMSREPORT")):
        check(transcripts[label] == f"@ERR,{command},2\n", "unsupported is not empty supported data")
    check("@OK,PING,0\n" in transcripts["overflow_recovery"] and
          "@OK,STOP" not in transcripts["overflow_recovery"], "bounded overflow recovery")
    lifetime, _ = parse("legacy_lifetime")
    check(lifetime[15].records[1].values == ("429496729500", "123456700"), "odometer scaling does not overflow u32")
    events, _ = parse("wrapping_events")
    event_records = [r for r in events[9].records if r.field_id == 3]
    check(len(event_records) == 8, "bounded RAM event snapshot")
    sequences = [int(r.values[1]) for r in event_records]
    check(all((b - a) % 65536 == 1 for a, b in zip(sequences, sequences[1:])), "wrapping chronology")
    check(any("newest" in str(row).lower() for row in desktop.da64_report_rows(events[9])), "desktop ordering metadata")
    interleaved, ordered = parse("interleaved_aggregate")
    check([r.report_id for r in ordered] == profile["aggregate_reports"] and
          "@OK,STOP,0\n" in transcripts["interleaved_aggregate"] and
          "@ERR,STATUS,20\n" in transcripts["interleaved_aggregate"], "notices/busy interleave only on line boundaries")
    busy_notice = desktop.Da64SerialReportParser().feed_line("@ERR,STATUS,20")
    check(isinstance(busy_notice, desktop.Da64ProtocolNotice) and
          busy_notice.message == "Firmware error 20", "busy must not inherit DA64 unknown-command error 1")

    # Parser must reject malformed/partial reports; no success from a mere ACK.
    for bad in ("@B,1,127\n", "@B,1,129\n@D,1,1,0\n@E,1,1\n",
                "@B,1,129\n@D,1,1,0,0,0,0000,0000\n@E,1,2\n"):
        instance = desktop.Da64SerialReportParser()
        items = [instance.feed_line(line) for line in bad.splitlines()]
        check(any(isinstance(item, desktop.Da64ProtocolError) for item in items) and
              not any(isinstance(item, desktop.Da64Report) for item in items), "reject malformed report")
    instance = desktop.Da64SerialReportParser()
    check(instance.feed_line("@B,1,129") is None and
          not isinstance(instance.feed_line("@OK,STATUS,0"), desktop.Da64Report), "ACK cannot complete report")

    # Exercise local PC integration without making CI depend on unpublished files.
    profile_tested = False
    if (args.desktop_source / 'buddy_software/cc2_firmware.py').is_file():
        sys.path.insert(0, str(args.desktop_source))
        from buddy_software.cc2_firmware import Cc2ReportProtocol
        from buddy_software.firmware_identity import parse_cc2_firmware_identity, merge_cc2_firmware_identity
        from buddy_software.protocols import Da64Report, Da64ProtocolError
        structured = parse_cc2_firmware_identity(expected_identity)
        legacy = parse_cc2_firmware_identity(transcripts['legacy_id'])
        for first, second in ((structured, legacy), (legacy, structured)):
            merged = merge_cc2_firmware_identity(first, second)
            check(merged.kind == 'lithium' and merged.source == 'IDENTIFY' and not merged.detail and
                  not merged.allows_legacy_service and merged.protocol_sha256 == digest,
                  'both probe orders preserve lithium identity and disable stock service')
        now = datetime.now(timezone.utc)
        for label in ('empty_report', 'valid_report', 'stale_report'):
            bound = Cc2ReportProtocol(ROOT / 'protocol/protocol.json', 'lithium', expected_sha256=digest)
            snapshots = {}
            for line in transcripts[label].splitlines():
                item = bound.feed_line(line)
                check(not isinstance(item, Da64ProtocolError), 'profile-bound parser: ' + label)
                if isinstance(item, Da64Report):
                    bound.rows(item)
                    snapshots[item.report_id] = SimpleNamespace(report=item, received_at=now.isoformat())
            live = bound.live_snapshot(snapshots, now)
            check(('soc_percent' in live) == (label == 'valid_report'), 'live SOC unavailable is never 255%')
            if label == 'valid_report':
                check(live['soc_percent'] == 50, 'profile live SOC')
                check(live['current_a'] == 10, 'lithium deciamps reach live current')
                check(live['total_voltage_v'] == 79.2, 'lithium centivolts reach live pack voltage')
                for line in transcripts['legacy_lifetime'].splitlines():
                    item = bound.feed_line(line)
                    check(not isinstance(item, Da64ProtocolError), 'profile-bound odometer parser')
                    if isinstance(item, Da64Report):
                        snapshots[item.report_id] = SimpleNamespace(report=item, received_at=now.isoformat())
                live = bound.live_snapshot(snapshots, now)
                check(live['odometer_km'] == 429496729.5, 'legacy metre odometer reaches live mileage without overflow')
                for missing in ('block_1_2_v', 'front_temp_c', 'rear_temp_c', 'box_temp_c'):
                    check(missing not in live, 'do not fabricate NiMH sensors: ' + missing)
            else:
                for missing in ('current_a', 'total_voltage_v'):
                    check(missing not in live, 'unavailable adapter does not publish live ' + missing)
        notice = bound.feed_line('@ERR,STATUS,20')
        check('busy' in notice.message.lower(), 'profile-specific error meanings')
        profile_tested = True
    report = {"assertions": checks, "failures": [], "protocol_sha256": digest,
              "desktop_source": str(source),
              "desktop_module_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "profile_bound_tested": profile_tested,
              "scope": "Actual desktop validator, parser, presenter and available profile/identity modules on compiled SDCC transcripts; no GUI or serial hardware test"}
    (build / "schema_verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
