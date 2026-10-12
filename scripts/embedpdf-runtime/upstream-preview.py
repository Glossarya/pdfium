#!/usr/bin/env python3
# Copyright 2026 CloudPDF LTD
# SPDX-License-Identifier: Apache-2.0

"""Preview merging upstream PDFium into the fork: the sync report's skeleton.

Runs a trial merge of <target> into HEAD (git merge-tree; nothing is written to
the working tree or any branch) and writes the sync report of
docs/embedpdf/upstream-sync.md with everything a script can know filled in:

  - upstream commits in the range, and the ones that look like security fixes;
  - conflict files and blocks, with the EmbedPDF(<rule>) tags each block
    carries and whether it holds an EPDF_TLS global;
  - the write audit: new upstream lines that change PDF objects, cast away
    const, add a cached object pointer, or call upstream's appearance
    generator;
  - upstream commits to the appearance generator (port or skip);
  - new upstream globals;
  - DEPS, pdfium.gni and build_overrides changes;
  - exported functions added, removed or changed.

Verdicts, resolutions and test results are left for whoever runs the sync.

Usage:
  upstream-preview.py <target> [--base COMMIT] [--out FILE]

  <target>  the upstream commit to merge, e.g. upstream/main or a month's last
            commit. Fetch upstream first:
              git fetch https://pdfium.googlesource.com/pdfium \\
                main:refs/remotes/upstream/main
  --base    the fork's current upstream base. Default: git merge-base HEAD
            <target>.
"""

import argparse
import collections
import datetime
import os
import re
import subprocess
import sys

SOURCE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TAG = re.compile(r'EmbedPDF\(([a-z0-9][a-z0-9-]*(?:, [a-z0-9][a-z0-9-]*)*)\)')
SECURITY = re.compile(
    r'overflow|crash|out[- ]of[- ]bounds|use[- ]after[- ]free|\buaf\b|'
    r'heap|stack[- ]buffer|null deref|fuzz|security|sanitiz|oob', re.I)
WRITE = re.compile(
    r'GetMutable\w*|SetNewFor|SetFor\(|RemoveFor|AppendNew|GetOrCreate\w*|'
    r'SetNewAt|NewIndirect|AddIndirectObject|\bSetData\b|\bSetRef\b')
CACHED_POINTER = re.compile(r'RetainPtr<(?:const )?CPDF_\w+>\s+\w+_\s*[;={]')
GLOBAL = re.compile(
    r'^\s*(?!return\b|delete\b|case\b|throw\b|if\b|else\b)(?:static\s+)?(?:std::atomic<[^>]+>|[A-Za-z_][\w:<>,*& ]*)\s+'
    r'(?:g_|s_|g[A-Z]|s[A-Z])\w*\s*(?:=|;|\{)')
NOT_BUILT = ('xfa/', 'fxjs/', 'v8/', 'fpdfsdk/fpdfxfa/', 'fpdfsdk/pwl/')
HUNK = re.compile(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@')
LOOKBACK_LINES = 15


def git(*args, check=True):
  result = subprocess.run(['git', '-C', SOURCE_DIR, *args],
                          capture_output=True,
                          text=True,
                          errors='replace')
  if check and result.returncode != 0:
    sys.exit(f'git {" ".join(args)} failed:\n{result.stderr}')
  return result.stdout


def short(commit):
  return git('rev-parse', '--short=9', commit).strip()


def is_source(path):
  return path.endswith(('.c', '.cc', '.cpp', '.h', '.mm'))


def is_test(path):
  return path.startswith('testing/') or 'test' in os.path.basename(path)


def commits(base, target, *paths):
  out = git('log', '--format=%h%x09%ad%x09%s', '--date=short',
            f'{base}..{target}', '--', *paths)
  return [line.split('\t', 2) for line in out.splitlines() if line]


def added_lines(base, target, paths):
  """Yields (path, new_line_number, text) for every added line."""
  diff = git('diff', '-U0', base, target, '--', *paths)
  path, line = None, 0
  for raw in diff.splitlines():
    if raw.startswith('+++ '):
      path = raw[6:] if raw.startswith('+++ b/') else None
    elif raw.startswith('@@'):
      m = HUNK.match(raw)
      line = int(m.group(1)) if m else 0
    elif raw.startswith('+') and path:
      yield path, line, raw[1:]
      line += 1


def conflicts(target):
  result = subprocess.run(
      ['git', '-C', SOURCE_DIR, 'merge-tree', '--write-tree', '--name-only',
       '--no-messages', 'HEAD', target],
      capture_output=True,
      text=True,
      errors='replace')
  if result.returncode not in (0, 1):
    sys.exit(f'git merge-tree failed:\n{result.stderr}')
  lines = result.stdout.splitlines()
  tree, files = lines[0], [f for f in lines[1:] if f]
  report = []
  for path in files:
    text = git('cat-file', '-p', f'{tree}:{path}', check=False).splitlines()
    blocks = []
    start = None
    for i, line in enumerate(text):
      if line.startswith('<<<<<<< '):
        start = i
      elif line.startswith('>>>>>>> ') and start is not None:
        window = '\n'.join(text[max(0, start - LOOKBACK_LINES):i + 1])
        blocks.append({
            'line': start + 1,
            'rules': sorted({r for t in TAG.findall(window)
                             for r in t.split(', ')}),
            'tls': 'EPDF_TLS' in '\n'.join(text[start:i + 1]),
        })
        start = None
    report.append((path, blocks))
  return report


def exported_functions(commit):
  """{name: normalized declaration} for every FPDF_EXPORT in public/."""
  names = git('ls-tree', '--name-only', commit, 'public/').split()
  out = {}
  for path in names:
    if not path.endswith('.h'):
      continue
    src = git('show', f'{commit}:{path}')
    for m in re.finditer(r'FPDF_EXPORT\s+(.*?);', src, re.S):
      decl = ' '.join(m.group(1).split())
      name = re.search(r'(\w+)\s*\(', decl)
      if name:
        out[name.group(1)] = decl
  return out


def section(title, lines):
  return [f'## {title}', ''] + (lines or ['None.']) + ['']


def main():
  parser = argparse.ArgumentParser(
      description=__doc__.split('\n\n')[0],
      formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument('target')
  parser.add_argument('--base')
  parser.add_argument('--out')
  args = parser.parse_args()

  target = short(args.target)
  base = short(args.base or git('merge-base', 'HEAD', args.target).strip())
  in_range = commits(base, target)
  today = datetime.date.today().isoformat()
  out = [f'# Sync {today}: upstream {base}..{target} ({len(in_range)} commits)',
         '']
  first, last = (in_range[-1][1], in_range[0][1]) if in_range else ('', '')
  out += section('Summary', [
      f'Upstream from {first} to {last}, merged into `{short("HEAD")}`.',
      '',
      '<two lines: what upstream changed that matters to us, what we had to '
      'do>',
  ])

  security = [c for c in in_range if SECURITY.search(c[2])]
  out += section('Likely security fixes', [
      f'- `{h}` {d} {s}' for h, d, s in security
  ])

  merged = conflicts(target)
  rows = []
  for path, blocks in merged:
    touched = commits(base, target, path)
    rules = sorted({r for b in blocks for r in b['rules']})
    tls = sum(b['tls'] for b in blocks)
    rows.append(
        f'| `{path}` | {len(blocks) or "binary, rename or delete"} | '
        f'{", ".join(rules) or "untagged"} | {"**" + str(tls) + "**" if tls else ""} | '
        + '; '.join(f'`{h}` {s}' for h, _, s in touched[:3]) +
        (f'; +{len(touched) - 3} more' if len(touched) > 3 else '') + ' | |')
  total = sum(len(b) for _, b in merged)
  out += section(
      f'Conflicts ({len(merged)} files, {total} blocks)',
      ['A block with an `EPDF_TLS` count is never resolved by taking '
       'upstream\'s side without putting `EPDF_TLS` back (rule '
       'thread-confined-globals).', '',
       '| File | Blocks | Rules in the blocks | EPDF_TLS | Upstream commits | '
       'Resolution |', '| :-- | --: | :-- | --: | :-- | :-- |'] + rows
      if rows else [])

  audit = []
  for path, line, text in added_lines(base, target, ['core', 'fpdfsdk']):
    if not is_source(path) or is_test(path):
      continue
    what = []
    if WRITE.search(text):
      what.append(WRITE.search(text).group(0))
    if 'const_cast' in text:
      what.append('const_cast')
    if CACHED_POINTER.search(text):
      what.append('cached object pointer')
    if 'CPDF_GenerateAP::' in text and 'generateap' not in path:
      what.append('CPDF_GenerateAP caller')
    if what:
      verdict = 'not-built' if path.startswith(NOT_BUILT) else ''
      audit.append(f'| `{path}:{line}` | {", ".join(what)} | {verdict} | |')
  out += section(f'Write audit ({len(audit)} lines)', [
      'Verdicts: `fresh` (created right there), `door` (through the copy-up '
      'door or its owner), `fixed` (broke a rule; fixed in this sync, with a '
      'tag), `not-built`.', '', '| File:line | What | Verdict | Why |',
      '| :-- | :-- | :-- | :-- |'
  ] + audit if audit else [])

  generator = commits(base, target, 'core/fpdfdoc/cpdf_generateap.cpp',
                      'core/fpdfdoc/cpdf_generateap.h')
  out += section('Upstream appearance generator', [
      'Our generator is separate (upstream-sync plan §6.2): port each commit '
      'or skip it.', '', '| Commit | Subject | Port or skip | Why |',
      '| :-- | :-- | :-- | :-- |'
  ] + [f'| `{h}` | {s} | | |' for h, _, s in generator] if generator else [])

  globals_found = [
      f'- `{p}:{n}`: `{t.strip()}`'
      for p, n, t in added_lines(base, target, ['core', 'fpdfsdk', 'third_party'])
      if is_source(p) and not is_test(p) and GLOBAL.search(t) and
      'constexpr' not in t and 'const ' not in t and '(' not in t
  ]
  out += section('Globals', [
      'New globals in upstream code (a heuristic); run '
      '`scripts/embedpdf-runtime/check-globals.py` on the merged build for '
      'the real list.', ''
  ] + globals_found if globals_found else [
      'No new globals by the heuristic; still run `check-globals.py` on the '
      'merged build.'
  ])

  deps = commits(base, target, 'DEPS')
  gni = [
      f'- `{p}:{n}`: `{t.strip()}`' for p, n, t in added_lines(
          base, target, ['pdfium.gni', 'build_overrides/pdfium.gni'])
      if '=' in t and not t.strip().startswith('#')
  ]
  out += section('Build and patches', [
      f'- `DEPS` changed in {len(deps)} commits.',
      '- Run `apply-patches.sh` for every target: a roll can break a patch in '
      '`patches/embedpdf-runtime/`.',
  ] + (['', 'Build arguments added or changed:'] + gni if gni else []))

  before, after = exported_functions(base), exported_functions(target)
  api = [f'- added `{n}`' for n in sorted(after.keys() - before.keys())]
  api += [f'- removed `{n}`' for n in sorted(before.keys() - after.keys())]
  api += [
      f'- changed `{n}`' for n in sorted(after.keys() & before.keys())
      if after[n] != before[n]
  ]
  out += section('API diff', api)

  out += section('Tests', [
      '- [ ] fork unit + embedder tests (`test-target.sh linux-x64`)',
      '- [ ] layer mode (`testing/tools/run_layer_mode.py`)',
      '- [ ] tag check (`check-fork-tags.py --strict`)',
      '- [ ] globals check (`check-globals.py`)',
      '- [ ] wasm build (`build-target.sh wasm32`)',
      '- [ ] ThreadSanitizer soak',
      '- [ ] engine tests and engine-conformance with the local runtime',
      '- changed expectations, each with its reason:',
  ])
  out += section('Ledger changes proposed',
                 ['<new rules or edits, for review; nothing applied without '
                  'approval>'])

  text = '\n'.join(out)
  if args.out:
    with open(args.out, 'w', encoding='utf-8') as f:
      f.write(text)
    print(f'wrote {args.out}: {len(in_range)} commits, {len(merged)} conflict '
          f'files, {total} blocks, {len(audit)} audit lines, '
          f'{len(security)} likely security fixes')
  else:
    print(text)


if __name__ == '__main__':
  main()
