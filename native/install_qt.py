"""Install the pinned Qt 6.12 MSVC SDK for builds; never used at runtime.

Qt 6.12 repositories are split by architecture. Pin official archive hashes
rather than relying on an installer that still assumes the older layout.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import urllib.request
import py7zr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    args = parser.parse_args()
    lock = json.loads(Path(__file__).with_name('qt-sdk.json').read_text('utf-8'))
    args.cache.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)

    def download(entry):
        archive = args.cache / (entry['module'] + '-' + lock['version'] + '.7z')
        if archive.is_file() and hashlib.sha256(archive.read_bytes()).hexdigest() == entry['sha256']:
            return archive
        temporary = archive.with_suffix('.download')
        with urllib.request.urlopen(entry['url'], timeout=60) as source, temporary.open('wb') as output:
            while chunk := source.read(1024 * 1024):
                output.write(chunk)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != entry['sha256']:
            raise RuntimeError('Qt archive checksum mismatch: ' + entry['module'])
        temporary.replace(archive)
        return archive

    with ThreadPoolExecutor(max_workers=3) as pool:
        archives = list(pool.map(download, lock['archives']))
    for archive in archives:
        print('Extracting ' + archive.name, flush=True)
        with py7zr.SevenZipFile(archive) as packed:
            for name in packed.getnames():
                if not (args.output / name).resolve().is_relative_to(args.output.resolve()):
                    raise RuntimeError('Qt archive path escapes its SDK directory')
            packed.extractall(path=args.output)
    (args.output / 'bin/qt.conf').write_text('[Paths]\nPrefix=..\n', encoding='utf-8')
    print('Qt ' + lock['version'] + ' SDK ready: ' + str(args.output.resolve()))


if __name__ == '__main__':
    main()
