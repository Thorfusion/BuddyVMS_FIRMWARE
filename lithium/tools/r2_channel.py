"""Package verified CC2 lithium builds and publish only their isolated R2 channel."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
from datetime import datetime

PREFIX = 'buddy/old-cc-firmware/lithium'
PROFILE = {'profile': 'CC2_LITHIUM', 'hardware': 'XC878', 'chemistry': 'LITHIUM'}
MAX_OBJECT_BYTES = 8 * 1024 * 1024


def encoded(value):
    return (json.dumps(value, indent=2, ensure_ascii=True) + '\n').encode('ascii')


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def version_key(version):
    if not isinstance(version, str) or not re.fullmatch(r'\d{8}-\d{6}\.\d+', version):
        raise ValueError('Invalid build ID')
    stamp, run = version.split('.')
    return datetime.strptime(stamp, '%Y%m%d-%H%M%S'), int(run)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_entry(entry):
    version = entry['version']
    stamp, _ = version_key(version)
    require(entry['date'] == stamp.strftime('%Y-%m-%d'), 'Release date does not match build ID')
    require(len(entry['firmware']) == 1, 'Channel must contain one operational image per build')
    image = entry['firmware'][0]
    require(all(image.get(k) == v for k, v in PROFILE.items()), 'Not CC2 lithium hardware')
    require(image.get('target') == 'OLD_CC' and image.get('part') == 'xc878', 'Not original CC/XC878')
    require(image.get('version') == version and image.get('date') == entry['date'], 'Firmware version mismatch')
    require(image.get('filename') == version + '.hex' and
            image.get('url') == f'releases/{version}/{version}.hex', 'Unexpected firmware path')
    require(bool(re.fullmatch('[0-9a-f]{64}', image.get('sha256', ''))), 'Invalid firmware digest')
    protocol = entry['protocol']
    require(protocol == image.get('protocol') and protocol.get('schema') == 1 and
            protocol.get('version') == version and protocol.get('date') == entry['date'] and
            protocol.get('filename') == 'protocol.json' and
            protocol.get('url') == f'releases/{version}/protocol.json' and
            bool(re.fullmatch('[0-9a-f]{64}', protocol.get('sha256', ''))), 'Invalid release protocol binding')


def validate_manifest(manifest):
    require(manifest.get('schema') == 2 and all(manifest.get(k) == v for k, v in PROFILE.items()),
            'Refusing an unrelated or unsupported channel manifest')
    versions = manifest.get('versions')
    require(isinstance(versions, list) and versions, 'Missing release history')
    seen = set()
    for entry in versions:
        validate_entry(entry)
        require(entry['version'] not in seen, 'Duplicate release version')
        seen.add(entry['version'])
    newest = max(versions, key=lambda v: version_key(v['version']))
    require(manifest.get('latest') == newest['version'] and manifest.get('firmware') == newest['firmware'] and
            manifest.get('protocol') == newest['protocol'], 'Latest pointer does not match history')


def merge_manifest(entry, previous=None):
    validate_entry(entry)
    versions = []
    if previous is not None:
        validate_manifest(previous)
        require(version_key(entry['version']) >= version_key(previous['latest']), 'Refusing channel rollback')
        versions = previous['versions']
        for older in versions:
            if older['version'] == entry['version']:
                require(older == entry, 'Build ID already names different release metadata')
    versions = [entry] + [v for v in versions if v['version'] != entry['version']]
    versions.sort(key=lambda v: version_key(v['version']), reverse=True)
    return {'schema': 2, **PROFILE, 'latest': entry['version'], 'firmware': entry['firmware'],
            'protocol': entry['protocol'], 'versions': versions}


def write_immutable(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        require(path.read_bytes() == raw, 'Refusing to replace immutable file: ' + str(path))
    else:
        path.write_bytes(raw)


def package(verified, output, previous=None):
    info = json.loads((verified / 'build.json').read_text(encoding='utf-8-sig'))
    require(info['profile'] == 'external-bms-default', 'SAFE/test image must not be the normal download')
    version = info['build_id']
    require(info.get('version', version) == version, 'Firmware version must match release build ID')
    stamp, _ = version_key(version)
    image_raw = (verified / 'CC2_LITHIUM.hex').read_bytes()
    protocol_raw = (verified / 'protocol.json').read_bytes()
    require(info['sha256'] == sha256(image_raw) and info['protocol_sha256'] == sha256(protocol_raw),
            'Verified HEX/protocol digest mismatch')
    require(info['p_used'] + info['p_free'] == 0xF000 and info['last_address'] < 0xF000 and
            info['d_used'] == 0, 'Image exceeds firmware memory layout')
    definition = json.loads(protocol_raw)
    require(definition['format'] == 'buddy.da64-report-protocol' and definition['schema'] == 1 and
            definition['cc2_profile']['id'] == 'CC2_LITHIUM' and
            definition['cc2_profile']['mcu'] == 'XC878' and
            definition['cc2_profile']['chemistry'] == 'LITHIUM' and
            definition['cc2_profile']['service_baud'] == 19200, 'Invalid lithium protocol')
    core = json.loads((verified / 'core-verification.json').read_text(encoding='utf-8-sig'))
    require(core['FAILURES'] == 0 and core['ASSERTIONS'] >= 600, 'Missing/passing core test required')
    for profile, name in (('build', 'CC2_LITHIUM.hex'), ('build_safe', 'CC2_LITHIUM_SAFE.hex')):
        profile_info = json.loads((verified / (profile + '.json')).read_text(encoding='utf-8-sig'))
        require(profile_info['build_id'] == version and profile_info['protocol_sha256'] == sha256(protocol_raw),
                'Profiles do not share build/protocol identity')
        require(profile_info['sha256'] == sha256((verified / name).read_bytes()), 'Profile HEX digest mismatch')
        for suffix in ('integration', 'contract'):
            report = json.loads((verified / f'{profile}-{suffix}.json').read_text(encoding='utf-8-sig'))
            require(report['assertions'] >= 50 and report['failures'] == [], 'Compiled integration/contract test failed')
            if suffix == 'integration':
                require(report['image_sha256'] == profile_info['sha256'], 'Tests refer to another image')
            else:
                require(report['protocol_sha256'] == sha256(protocol_raw), 'Tests refer to another protocol')
    date = stamp.strftime('%Y-%m-%d')
    protocol = {'schema': 1, 'version': version, 'date': date, 'filename': 'protocol.json',
                'url': f'releases/{version}/protocol.json', 'sha256': sha256(protocol_raw)}
    image = {'target': 'OLD_CC', 'part': 'xc878', **PROFILE, 'version': version, 'date': date,
             'filename': version + '.hex', 'url': f'releases/{version}/{version}.hex',
             'sha256': sha256(image_raw), 'protocol': protocol,
             'notes': 'PoldenTEK CC2 lithium bench firmware. EXTERNAL/FORCE defaults require an independent lithium charger and BMS. Hardware validation not claimed.'}
    entry = {'version': version, 'date': date, 'firmware': [image], 'protocol': protocol}
    manifest = merge_manifest(entry, previous)
    release = output / 'releases' / version
    write_immutable(release / (version + '.hex'), image_raw)
    write_immutable(release / 'protocol.json', protocol_raw)
    write_immutable(release / 'release.json', encoded(entry))
    output.mkdir(parents=True, exist_ok=True)
    (output / 'protocol.json').write_bytes(protocol_raw)
    (output / 'manifest.json').write_bytes(encoded(manifest))
    return manifest


def publish(channel, store):
    """Store keys are always relative to PREFIX. Never delete or touch another channel."""
    manifest = json.loads((channel / 'manifest.json').read_bytes())
    validate_manifest(manifest)
    version = manifest['latest']
    entry = next(v for v in manifest['versions'] if v['version'] == version)
    release = channel / 'releases' / version
    assets = {f'{version}.hex': entry['firmware'][0]['sha256'], 'protocol.json': entry['protocol']['sha256'],
              'release.json': sha256(encoded(entry))}
    raw_assets = {}
    for name, digest in assets.items():
        raw_assets[name] = (release / name).read_bytes()
        require(sha256(raw_assets[name]) == digest, 'Local package digest mismatch: ' + name)
    previous_raw = store.get('manifest.json')
    merged = merge_manifest(entry, json.loads(previous_raw) if previous_raw is not None else None)
    for name, raw in raw_assets.items():
        key = f'releases/{version}/{name}'
        store.put_immutable(key, raw)
        require(store.get(key) == raw, 'Uploaded object read-back mismatch: ' + key)
    require(store.get('manifest.json') == previous_raw, 'Channel changed while uploading; retry publication')
    store.put_pointer('protocol.json', raw_assets['protocol.json'])
    store.put_pointer('manifest.json', encoded(merged))
    return merged


class R2Store:
    def __init__(self):
        import boto3
        from botocore.config import Config
        from botocore.exceptions import ClientError
        self.client_error = ClientError
        for name in ('R2_BUCKET', 'R2_ENDPOINT_URL', 'AWS_ACCESS_KEY_ID', 'AWS_SECRET_ACCESS_KEY'):
            require(bool(os.environ.get(name)), 'Missing publishing secret: ' + name)
        self.bucket = os.environ['R2_BUCKET']
        endpoint = os.environ['R2_ENDPOINT_URL']
        require(endpoint.startswith('https://'), 'R2 endpoint must use HTTPS')
        self.client = boto3.client('s3', endpoint_url=endpoint, region_name='auto',
                                   config=Config(signature_version='s3v4',
                                                 request_checksum_calculation='when_required',
                                                 response_checksum_validation='when_required',
                                                 retries={'max_attempts': 5, 'mode': 'standard'}))
        self.manifest_etag = None

    def key(self, key):
        require(key in ('manifest.json', 'protocol.json') or
                re.fullmatch(r'releases/\d{8}-\d{6}\.\d+/[A-Za-z0-9_.-]+', key), 'Invalid channel key')
        return PREFIX + '/' + key

    def get(self, key):
        try:
            response = self.client.get_object(Bucket=self.bucket, Key=self.key(key))
        except self.client_error as exc:
            if exc.response['ResponseMetadata']['HTTPStatusCode'] == 404:
                if key == 'manifest.json':
                    self.manifest_etag = None
                return None
            raise
        if key == 'manifest.json':
            self.manifest_etag = response['ETag']
        with response['Body'] as stream:
            raw = stream.read(MAX_OBJECT_BYTES + 1)
        require(len(raw) <= MAX_OBJECT_BYTES, 'Channel object is too large')
        return raw

    def put_immutable(self, key, raw):
        try:
            self.client.put_object(Bucket=self.bucket, Key=self.key(key), Body=raw, IfNoneMatch='*',
                                   ContentType='application/json' if key.endswith('.json') else 'application/octet-stream',
                                   CacheControl='public,max-age=31536000,immutable')
        except self.client_error as exc:
            if exc.response['ResponseMetadata']['HTTPStatusCode'] != 412:
                raise
            require(self.get(key) == raw, 'R2 build ID already contains different bytes: ' + key)

    def put_pointer(self, key, raw):
        require(key in ('manifest.json', 'protocol.json'), 'Only latest pointers are mutable')
        condition = {}
        if key == 'manifest.json':
            condition = {'IfMatch': self.manifest_etag} if self.manifest_etag else {'IfNoneMatch': '*'}
        self.client.put_object(Bucket=self.bucket, Key=self.key(key), Body=raw,
                               ContentType='application/json', CacheControl='no-cache', **condition)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    pack = sub.add_parser('package')
    pack.add_argument('--verified', type=Path, required=True)
    pack.add_argument('--output', type=Path, required=True)
    pack.add_argument('--previous', type=Path)
    upload = sub.add_parser('publish')
    upload.add_argument('--channel', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'package':
        previous = json.loads(args.previous.read_bytes()) if args.previous else None
        result = package(args.verified, args.output, previous)
    else:
        result = publish(args.channel, R2Store())
    print(json.dumps({'latest': result['latest'], 'versions': len(result['versions']), 'prefix': PREFIX}))
