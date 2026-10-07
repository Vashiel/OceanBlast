"""Check tracked UTF-8 files for personal Windows user paths or supplied patterns."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pattern', action='append', default=[], help='additional regular expression to reject')
    args = parser.parse_args()
    patterns = [re.compile(r'\b[A-Z]:[\\/]+Users[\\/]+[^\\/\s]+', re.IGNORECASE)]
    patterns += [re.compile(pattern, re.IGNORECASE) for pattern in args.pattern]
    root = Path(subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip())
    files = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z']).decode('utf-8').split('\0')
    matches = []
    for name in files:
        if not name or not (root / name).is_file():
            continue
        try:
            text = (root / name).read_text(encoding='utf-8')
        except (UnicodeError, OSError):
            continue
        if any(pattern.search(text) for pattern in patterns):
            matches.append(name)
    for name in matches:
        print('Rejected pattern in:', name)
    print('Tracked-file check:', 'FAILED' if matches else 'PASS')
    return bool(matches)


if __name__ == '__main__':
    raise SystemExit(main())
