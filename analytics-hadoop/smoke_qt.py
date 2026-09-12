"""Exercise real Qt HTTP endpoints using a disposable copy of the business DB."""
import argparse
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', required=True)
    parser.add_argument('--database', required=True)
    parser.add_argument('--result', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = sqlite3.connect(Path(args.database).resolve().as_uri() + '?mode=ro', uri=True)
        copy = sqlite3.connect(root / 'copy.db')
        source.backup(copy)
        copy.close()
        source.close()
        result = root / 'analytics.json'
        original = Path(args.result).read_bytes()
        result.write_bytes(original)
        env = {**os.environ, 'ANALYTICS_RESULT_PATH': str(result), 'ANALYTICS_MAX_AGE_SECONDS': '1'}
        server = subprocess.Popen([args.server, '--database', str(root / 'copy.db'), '--host', '127.0.0.1',
                                   '--port', '19020', '--dashboard-port', '19021'], env=env,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        def get(path):
            try:
                response = urllib.request.urlopen('http://127.0.0.1:19021' + path, timeout=3)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                return response.status, json.load(response)
        try:
            for _ in range(50):
                if server.poll() is not None:
                    raise RuntimeError('Qt server exited during startup')
                try:
                    status, data = get('/api/dashboard')
                    if data.get('generated_at'):
                        break
                except OSError:
                    pass
                time.sleep(.2)
            else:
                raise RuntimeError('Qt server did not become ready')
            assert status == 200 and isinstance(data['stations'], list)
            status, payload = get('/api/analytics')
            assert status == 200 and payload['available'] and payload['stale']
            assert payload['data'] == json.loads(original)
            result.write_text('broken', encoding='utf-8')
            status, payload = get('/api/analytics')
            assert status == 503 and payload['reason'] == 'invalid_result'
            result.unlink()
            status, payload = get('/api/analytics')
            assert status == 503 and payload['reason'] == 'missing_result'
            result.write_bytes(original)
            assert get('/api/analytics')[0] == 200
            assert get('/api/dashboard')[0] == 200
            print('PASS: Qt live data, Hadoop payload, stale, invalid, missing and recovery HTTP checks')
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == '__main__':
    main()
