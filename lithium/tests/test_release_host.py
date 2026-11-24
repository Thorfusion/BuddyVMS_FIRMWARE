"""R2 contract and failure-path tests; no cloud credentials or network used."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('channel', ROOT / 'tools/r2_channel.py')
channel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(channel)


class MemoryStore:
    def __init__(self):
        self.objects = {}
        self.writes = []
        self.bad_readback = False

    def get(self, key):
        if self.bad_readback and key.endswith('.hex'):
            return b'corrupt upload'
        return self.objects.get(key)

    def put_immutable(self, key, raw):
        if key in self.objects and self.objects[key] != raw:
            raise ValueError('Immutable conflict')
        self.objects[key] = raw
        self.writes.append(key)

    def put_pointer(self, key, raw):
        self.objects[key] = raw
        self.writes.append(key)


class ChannelTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.verified = self.root / 'verified'
        self.verified.mkdir()
        self.output = self.root / 'channel'
        self.version = '20261124-020304.42'
        self.protocol = (ROOT / 'protocol/protocol.json').read_bytes()
        (self.verified / 'protocol.json').write_bytes(self.protocol)
        for profile, name in (('build', 'CC2_LITHIUM.hex'), ('build_safe', 'CC2_LITHIUM_SAFE.hex')):
            raw = name.encode('ascii')  # Transport/package fixture, not a valid firmware image.
            (self.verified / name).write_bytes(raw)
            info = {'profile': 'external-bms-default' if profile == 'build' else 'safe',
                    'build_id': self.version, 'sha256': channel.sha256(raw),
                    'protocol_sha256': channel.sha256(self.protocol), 'p_used': 35000,
                    'p_free': 26440, 'last_address': 35058, 'd_used': 0}
            self.write(profile + '.json', info)
            self.write(profile + '-integration.json', {'assertions': 100, 'failures': [], 'image_sha256': info['sha256']})
            self.write(profile + '-contract.json', {'assertions': 60, 'failures': [], 'protocol_sha256': info['protocol_sha256']})
        self.write('core-verification.json', {'ASSERTIONS': 811, 'FAILURES': 0})

    def write(self, name, value):
        (self.verified / name).write_bytes(channel.encoded(value))

    def build(self, previous=None):
        return channel.package(self.verified, self.output, previous)

    def test_dated_schema2_package_and_safe_exclusion(self):
        manifest = self.build()
        channel.validate_manifest(manifest)
        self.assertEqual(manifest['latest'], self.version)
        self.assertEqual(manifest['protocol']['version'], self.version)
        names = {p.name for p in (self.output / 'releases' / self.version).iterdir()}
        self.assertEqual(names, {self.version + '.hex', 'protocol.json', 'release.json'})
        self.assertEqual(manifest['firmware'][0]['hardware'], 'XC878')
        self.assertEqual(manifest['firmware'][0]['part'], 'xc878')

    def test_repeat_package_is_identical(self):
        first = self.build()
        self.assertEqual(self.build(first), first)

    def test_firmware_version_cannot_differ_from_release_build_id(self):
        info = json.loads((self.verified / 'build.json').read_bytes())
        info['version'] = '0.2.2'
        self.write('build.json', info)
        with self.assertRaisesRegex(ValueError, 'version must match'):
            self.build()
        info['version'] = self.version
        self.write('build.json', info)
        self.assertEqual(self.build()['latest'], self.version)

    def test_history_is_preserved_and_rollback_rejected(self):
        current = self.build()['versions'][0]
        older = json.loads(json.dumps(current).replace(self.version, '20261123-020304.41').replace('2026-11-24', '2026-11-23'))
        previous = channel.merge_manifest(older)
        merged = channel.merge_manifest(current, previous)
        self.assertEqual([v['version'] for v in merged['versions']], [self.version, older['version']])
        self.assertEqual(merged['versions'][1], previous['versions'][0])
        with self.assertRaisesRegex(ValueError, 'rollback'):
            channel.merge_manifest(older, merged)

    def test_reused_build_id_cannot_change_hash(self):
        first = self.build()
        entry = copy.deepcopy(first['versions'][0])
        entry['firmware'][0]['sha256'] = 'a' * 64
        with self.assertRaisesRegex(ValueError, 'different release'):
            channel.merge_manifest(entry, first)

    def test_reject_foreign_profile_and_urls(self):
        first = self.build()
        for mutate in (lambda m: m.update(profile='CC2_NIMH'),
                       lambda m: m['versions'][0]['firmware'][0].update(url='../nimh/x.hex'),
                       lambda m: m.update(latest='19990101-000000.1')):
            altered = copy.deepcopy(first)
            mutate(altered)
            with self.assertRaises(ValueError):
                channel.validate_manifest(altered)

    def test_corrupt_hex_and_wrong_test_image_are_rejected(self):
        (self.verified / 'CC2_LITHIUM.hex').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'digest'):
            self.build()

    def test_failed_core_or_integration_blocks_package(self):
        self.write('core-verification.json', {'ASSERTIONS': 811, 'FAILURES': 1})
        with self.assertRaisesRegex(ValueError, 'core'):
            self.build()
        self.write('core-verification.json', {'ASSERTIONS': 811, 'FAILURES': 0})
        self.write('build-integration.json', {'assertions': 100, 'failures': ['bad']})
        with self.assertRaisesRegex(ValueError, 'test failed'):
            self.build()

    def test_publish_manifest_last_and_idempotent(self):
        expected = self.build()
        store = MemoryStore()
        self.assertEqual(channel.publish(self.output, store), expected)
        self.assertEqual(store.writes[-2:], ['protocol.json', 'manifest.json'])
        self.assertTrue(all(key.startswith('releases/' + self.version + '/') for key in store.writes[:-2]))
        channel.publish(self.output, store)
        self.assertEqual(json.loads(store.objects['manifest.json']), expected)

    def test_corrupt_readback_does_not_publish_latest(self):
        self.build()
        store = MemoryStore()
        store.bad_readback = True
        with self.assertRaisesRegex(ValueError, 'read-back'):
            channel.publish(self.output, store)
        self.assertNotIn('manifest.json', store.objects)
        self.assertNotIn('protocol.json', store.objects)

    def test_remote_collision_preserves_latest(self):
        self.build()
        store = MemoryStore()
        store.objects[f'releases/{self.version}/{self.version}.hex'] = b'another build'
        with self.assertRaisesRegex(ValueError, 'Immutable'):
            channel.publish(self.output, store)
        self.assertNotIn('manifest.json', store.objects)

    def test_remote_permission_error_is_not_empty_history(self):
        self.build()
        class Denied(MemoryStore):
            def get(self, key):
                raise PermissionError('403')
        with self.assertRaises(PermissionError):
            channel.publish(self.output, Denied())

    def test_publish_merges_remote_history_not_just_artifact_history(self):
        current = self.build()['versions'][0]
        older = json.loads(json.dumps(current).replace(self.version, '20261123-020304.41').replace('2026-11-24', '2026-11-23'))
        store = MemoryStore()
        store.objects['manifest.json'] = channel.encoded(channel.merge_manifest(older))
        result = channel.publish(self.output, store)
        self.assertEqual(len(result['versions']), 2)

    def test_r2_prefix_cannot_escape_lithium(self):
        store = channel.R2Store.__new__(channel.R2Store)
        self.assertEqual(store.key('manifest.json'), 'buddy/old-cc-firmware/lithium/manifest.json')
        for key in ('../nimh/manifest.json', '/manifest.json', 'releases/../x.hex', 'anything.json'):
            with self.assertRaises(ValueError):
                store.key(key)

    def test_invalid_calendar_and_traversal_build_ids(self):
        for value in ('20260230-120000.1', '../test', '0.2.1', '20261124-250000.1'):
            with self.assertRaises(ValueError):
                channel.version_key(value)

    def test_concurrent_manifest_change_aborts_latest(self):
        self.build()
        class Changed(MemoryStore):
            reads = 0
            def get(self, key):
                if key == 'manifest.json':
                    self.reads += 1
                    if self.reads > 1:
                        return b'another publisher changed history'
                return super().get(key)
        store = Changed()
        with self.assertRaisesRegex(ValueError, 'changed while uploading'):
            channel.publish(self.output, store)
        self.assertNotIn('manifest.json', store.writes)

    def test_s3_manifest_pointer_uses_atomic_precondition(self):
        class Client:
            writes = []
            def get_object(self, **kwargs):
                return {'ETag': '"previous-etag"', 'Body': io.BytesIO(b'previous')}
            def put_object(self, **kwargs):
                self.writes.append(kwargs)
        store = channel.R2Store.__new__(channel.R2Store)
        store.client = Client()
        store.client_error = RuntimeError
        store.bucket = 'test'
        store.manifest_etag = None
        store.put_pointer('manifest.json', b'first')
        self.assertEqual(store.client.writes[-1]['IfNoneMatch'], '*')
        self.assertEqual(store.get('manifest.json'), b'previous')
        store.put_pointer('manifest.json', b'next')
        self.assertEqual(store.client.writes[-1]['IfMatch'], '"previous-etag"')
        store.put_immutable('releases/' + self.version + '/protocol.json', b'protocol')
        self.assertEqual(store.client.writes[-1]['IfNoneMatch'], '*')

    def test_s3_missing_object_is_distinct_from_permission_failure(self):
        class ClientError(Exception):
            def __init__(self, status):
                self.response = {'ResponseMetadata': {'HTTPStatusCode': status}}
        class Client:
            status = 404
            def get_object(self, **kwargs):
                raise ClientError(self.status)
        store = channel.R2Store.__new__(channel.R2Store)
        store.client = Client()
        store.client_error = ClientError
        store.bucket = 'test'
        self.assertIsNone(store.get('manifest.json'))
        store.client.status = 403
        with self.assertRaises(ClientError):
            store.get('manifest.json')


if __name__ == '__main__':
    unittest.main()
