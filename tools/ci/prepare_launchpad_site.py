#!/usr/bin/env python3
"""Package Launchpad firmware and configuration for GitHub Pages (Python 3.11+)."""

import argparse
import os
import shutil
import tomllib
from pathlib import Path
from urllib.parse import quote


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pages-url', default='https://espressif.github.io/esp-thread-br')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    source = repo / 'docs/launchpad'
    images = repo / 'build/launchpad/images'
    config = tomllib.loads((source / 'launchpad.toml').read_text())
    artifacts = []
    for app in config['supported_apps']:
        image = images / config[app]['image']['esp32s3']
        record = image.with_suffix('.txt')
        for artifact in (image, record):
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise SystemExit(f'Missing Launchpad artifact: {artifact}')
        artifacts.append((app, image, record))

    revisions = None
    for app, _, record in artifacts:
        current = dict(line.split(': ', 1) for line in record.read_text().splitlines())
        if revisions is None:
            revisions = current
        elif current != revisions:
            raise SystemExit(f'Firmware build commits differ: {app}')

    destination = repo / 'build/launchpad/site/launchpad'
    shutil.copytree(source, destination, dirs_exist_ok=True)
    canonical_url = 'https://espressif.github.io/esp-thread-br'
    pages_url = args.pages_url.rstrip('/')
    for name in ('launchpad.toml', 'build_cfg.md', 'index.html'):
        path = destination / name
        content = path.read_text().replace(canonical_url, pages_url)
        content = content.replace(quote(canonical_url, safe=''), quote(pages_url, safe=''))
        path.write_text(content)
    for _, image, record in artifacts:
        for artifact in (image, record):
            shutil.copy2(artifact, destination / artifact.name)
    with (destination / 'build_cfg.md').open('a') as output:
        output.write('\n## Firmware commits\n')
        repositories = {
            'esp-thread-br': os.environ.get('GITHUB_REPOSITORY', 'espressif/esp-thread-br'),
            'esp-idf': 'espressif/esp-idf',
        }
        for project in ('esp-thread-br', 'esp-idf'):
            revision = revisions[project]
            url = f'https://github.com/{repositories[project]}/commit/{revision}'
            output.write(f'\n* {project}: [{revision[:8]}]({url})\n')


if __name__ == '__main__':
    main()
