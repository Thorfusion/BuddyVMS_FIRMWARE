"""Offline, schema-driven checks of compiled UART captures, independent of a PC checkout."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def parse_reports(text, definition):
    reports = []
    active = None
    for line in text.splitlines():
        parts = line.split(',')
        if parts[0] in ('@OK', '@ERR'):
            continue
        if parts[0] == '@B':
            assert active is None and len(parts) == 3, line
            report_id, version = map(int, parts[1:])
            assert definition['cc2_profile']['supported_reports'].get(str(report_id)) == version, line
            active = {'id': report_id, 'version': version, 'records': []}
        elif parts[0] == '@D':
            assert active and len(parts) >= 4 and int(parts[1]) == active['id'], line
            fields = definition['reports'][str(active['id'])]['fields']
            columns = fields[parts[2]]['columns']
            assert len(columns) == len(parts[3:]), line
            for name, value in zip(columns, parts[3:]):
                assert re.fullmatch(r'[0-9A-F]+' if name.endswith('_hex') else r'-?\d+', value), line
                if name in definition['boolean_fields']:
                    assert value in ('0', '1'), line
            active['records'].append((int(parts[2]), dict(zip(columns, parts[3:]))))
        elif parts[0] == '@E':
            assert active and len(parts) == 3 and int(parts[1]) == active['id'], line
            assert int(parts[2]) == len(active['records']), line
            reports.append(active)
            active = None
        else:
            raise AssertionError('Unexpected report line: ' + line)
    assert active is None, 'Unterminated report'
    return reports


def verify(build):
    raw = (ROOT / 'protocol/protocol.json').read_bytes()
    definition = json.loads(raw)
    info = json.loads((build / 'build_info.json').read_text())
    transcripts = json.loads((build / 'report_transcripts.json').read_text())
    checks = 0

    def check(condition, label):
        nonlocal checks
        checks += 1
        assert condition, label

    digest = hashlib.sha256(raw).hexdigest()
    check(info['protocol_sha256'] == digest, 'Protocol hash matches build')
    check(info['sha256'] == hashlib.sha256((build / info['image']).read_bytes()).hexdigest(), 'HEX hash matches build')
    check(info['p_used'] + info['p_free'] == 0xF000 and info['last_address'] < 0xF000, 'P-Flash limits')
    identity = f"@OK,IDENTIFY,1,CC2_LITHIUM,XC878,LITHIUM,{info['build_id']},{digest}\n"
    for label in ('identify', 'native_id', 'lf_identify', 'lf_id', 'crlf_id', 'wrapped_identify', 'after_tx_abort_identify'):
        check(transcripts[label] == identity, label)
    version = info['version']
    check(version == info['build_id'], 'Firmware and release version are identical')
    check(transcripts['legacy_id'] == f'$ID,BUDDY-CC2-LITHIUM,{version}#\n', 'Desktop legacy identity')
    for label in ('empty_report', 'valid_report', 'stale_report', 'interleaved_aggregate'):
        reports = parse_reports(transcripts[label], definition)
        check([r['id'] for r in reports] == definition['cc2_profile']['aggregate_reports'], label + ' aggregate order')
        status = next(r for r in reports if r['id'] == 1)
        soc = [v['soc_pct'] for _, v in status['records'] if 'soc_pct' in v]
        check(soc == (['50'] if label == 'valid_report' else []), label + ' SOC validity')
        for report in reports:
            check(report['version'] in definition['reports'][str(report['id'])]['schema_versions'], 'Schema version')
            if label in ('empty_report', 'stale_report') and report['id'] in (3, 4):
                check([field for field, _ in report['records']] == [1], 'No invented/stale measurements')
    for label in ('legacy_lifetime', 'wrapping_events', 'bms_status'):
        check(bool(parse_reports(transcripts[label], definition)), label)
    for label, command in (('unsupported_log', 'LOG'), ('unsupported_journal', 'READJOURNAL'),
                           ('unsupported_bmsreport', 'BMSREPORT')):
        check(transcripts[label] == f'@ERR,{command},2\n', label)
    expected_interlocks = ''.join(f'@ERR,{command},4\n' for command in
                                  ('BMS', 'CONFIG', 'CONFIG', 'CONFIG', 'CONFIG', 'SPEEDCAL', 'EEPROM', 'CHARGE', 'DRIVE'))
    for cause in ('charger', 'ignition', 'motion', 'drive_outputs'):
        check(transcripts['interlocks_' + cause] == expected_interlocks, 'Recognized interlocked commands: ' + cause)
    check(transcripts['service_parameter_errors'] ==
          '@ERR,CONFIG,3\n@ERR,CONFIG,5\n@ERR,SPEEDCAL,3\n@ERR,SPEEDCAL,3\n'
          '@ERR,EEPROM,3\n@ERR,EEPROM,3\n@ERR,CONFIG,2\n', 'Syntax, limits and unsupported are distinct')
    for label in ('rx_recovery_hash', 'rx_recovery_dollar'):
        check(transcripts[label] == '@OK,PING,0\n', 'Fresh delimiter recovers RX: ' + label)
    for malformed in ('@B,1,128\n', '@B,1,129\n@D,1,1,0\n@E,1,1\n',
                      '@B,1,129\n@D,1,3,50\n@E,1,2\n', '@B,1,129\n'):
        try:
            parse_reports(malformed, definition)
        except (AssertionError, KeyError, ValueError):
            check(True, 'Malformed envelope rejected')
        else:
            check(False, 'Malformed envelope accepted')
    result = {'assertions': checks, 'failures': [], 'protocol_sha256': digest,
              'scope': 'Offline JSON contract on actual compiled captures; not a desktop or hardware test'}
    (build / 'contract_verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='ascii')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--safe', action='store_true')
    args = parser.parse_args()
    verify(ROOT / 'firmware' / ('build_safe' if args.safe else 'build'))
