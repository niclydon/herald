#!/usr/bin/env python3
"""Scan reachable Git history and the publishable worktree, never printing secrets.

Requires Gitleaks 8.x on PATH (or GITLEAKS=/path/to/gitleaks).
Reviewed upstream exceptions match rule + path + SHA256 of the entire file.
Changing even one byte requires review again; no paths/rules are blanket ignored.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def main():
    executable = os.environ.get('GITLEAKS', 'gitleaks')
    if not shutil.which(executable):
        raise SystemExit('Install Gitleaks 8.x or set GITLEAKS to its executable.')
    reviewed = json.loads((ROOT / '.herald-audit-allowlist.json').read_text())
    allowed = {(x['rule'], x['path'], x['sha256']) for x in reviewed}
    paths = sorted(set(git('ls-files', '-z', '--cached', '--others', '--exclude-standard').decode().split('\0')) - {''})
    with tempfile.TemporaryDirectory(prefix='herald-public-audit-') as tmp:
        tmp = Path(tmp)
        tree = tmp / 'source'
        tree.mkdir()
        count = 0
        for name in paths:
            source = ROOT / name
            if not source.exists():
                continue  # tracked deletion is not part of the public worktree
            if (source.suffix in {'.bin', '.elf', '.log'} or
                    source.name.startswith('sdkconfig.private') or
                    source.name == '.env' or source.name.startswith('.env.') and source.name != '.env.example' or
                    '.private' in source.parts):
                raise SystemExit(f'Private/generated artifact in publishable file list: {name}')
            if source.is_symlink():
                raise SystemExit(f'Review symlink before publishing: {name}')
            destination = tree / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            count += 1
        failures = 0
        for mode, source in [('git', ROOT), ('dir', tree)]:
            report = tmp / f'{mode}.json'
            cmd = [executable, mode, str(source), '--redact', '--no-banner',
                   '--report-format', 'json', '--report-path', str(report),
                   '--config', str(ROOT / '.gitleaks.toml')]
            if mode == 'git':
                cmd.append('--log-opts=--all')
            run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if run.returncode not in (0, 1) or not report.exists():
                raise SystemExit(f'Gitleaks {mode} scan failed; no clean audit claimed.')
            findings = json.loads(report.read_text())
            accepted = 0
            for item in findings:
                name = item['File']
                if mode == 'git':
                    data = git('show', f"{item['Commit']}:{name}")
                else:
                    p = Path(name)
                    if p.is_absolute():
                        name = p.relative_to(tree).as_posix()
                    data = (tree / name).read_bytes()
                key = (item['RuleID'], name, hashlib.sha256(data).hexdigest())
                if key in allowed:
                    accepted += 1
                else:
                    failures += 1
                    print(f"REVIEW {mode}: {name}:{item['StartLine']} rule={item['RuleID']}")
            print(f'{mode}: {len(findings)} findings; {accepted} reviewed exact-content exceptions')
        print(f'Publishable files: {count}; unreviewed findings: {failures}')
        return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
