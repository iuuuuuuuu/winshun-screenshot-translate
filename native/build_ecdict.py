"""构建固定版本的 ECDICT 索引；仅开发 / 打包时需要 Python。"""
import csv
import hashlib
import io
import json
from pathlib import Path
import struct
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REVISION = '39cfb9672c0906b63569e0453c2e6abfb9ab182a'
SOURCE_SHA256 = 'd1ecbdf9ee2b920afb6f430ca6b6bb9f559588cf0dff86e04bd255f9390843cc'
URL = f'https://raw.githubusercontent.com/skywind3000/ECDICT/{REVISION}/ecdict.csv'


def main():
    dest = ROOT/'data/ecdict.bin'
    metadata = dest.with_suffix('.json')
    if dest.exists() and metadata.exists():
        info = json.loads(metadata.read_text('utf-8'))
        if info.get('sourceSha256') == SOURCE_SHA256 and hashlib.sha256(dest.read_bytes()).hexdigest() == info.get('sha256'):
            print(f'ECDICT: {info["entries"]} entries verified')
            return
    cached = ROOT/'.test-output/dictionary-research/ecdict-data.txt'
    raw = cached.read_bytes() if cached.exists() else urllib.request.urlopen(URL, timeout=120).read()
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise RuntimeError('ECDICT 源文件校验失败')
    entries = {}
    for row in csv.DictReader(io.StringIO(raw.decode('utf-8-sig'))):
        key = ' '.join(row['word'].strip().lower().split()).encode('utf-8')
        if not key or not (row['translation'] or row['definition']):
            continue
        value = [row.get(k, '').replace('\\n', '\n') for k in ('phonetic', 'translation', 'definition', 'exchange')]
        entries[key] = json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    dest.parent.mkdir(exist_ok=True)
    temporary = dest.with_suffix('.tmp')
    with temporary.open('wb') as out:
        out.write(b'ECDICT01' + struct.pack('<I', len(entries)))
        offset = 12 + len(entries)*12
        for key, value in sorted(entries.items()):
            out.write(struct.pack('<III', offset, len(key), len(value)))
            offset += len(key) + len(value)
        for key, value in sorted(entries.items()):
            out.write(key + value)
    temporary.replace(dest)
    info = dict(source=URL, revision=REVISION, sourceSha256=SOURCE_SHA256, license='MIT',
                entries=len(entries), size=dest.stat().st_size, sha256=hashlib.sha256(dest.read_bytes()).hexdigest())
    metadata.write_text(json.dumps(info, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(info))


if __name__ == '__main__':
    main()
