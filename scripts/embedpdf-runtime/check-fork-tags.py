#!/usr/bin/env python3
# Copyright 2026 CloudPDF LTD
# SPDX-License-Identifier: Apache-2.0

"""Check that every EmbedPDF change inside an upstream PDFium file names its rule.

An upstream file is a C or C++ source that exists at the upstream base commit.
A change block is a run of changed lines in `git diff <base>` (the working
tree against the base) for such a file; changes at most 3 lines apart count as one block. A block passes when its
new lines, or the 15 lines above them, hold `EmbedPDF(<rule>)` and <rule> is a
`### <rule>` heading in docs/embedpdf/fork-ledger.md. A tag may name several
rules: `EmbedPDF(<rule>, <rule>)`.

Exempt without any entry: blocks that only change #include lines, and blocks
that only change comments or blank lines. Whole files can be exempted in
scripts/embedpdf-runtime/fork-tags-allowlist.txt, one `<path> <reason>` per
line. Tests (testing/ and *test* files) and build files aren't checked.

Statuses:
  tagged        names a rule from the ledger
  unknown-rule  names a rule the ledger doesn't have
  needs-rule    has an EmbedPDF comment, but no (rule)
  untagged      no EmbedPDF comment at all
  exempt        #include-only or comment-only, or an allowlisted file

Usage:
  check-fork-tags.py [--base COMMIT] [--strict] [--format text|json] [PATH...]

  --base     the upstream commit this fork is based on. Default: the merge
             base of HEAD with upstream/main, pdfium/main or
             origin/pdfium/main, the first that exists.
  --strict   exit 1 when a block is unknown-rule, needs-rule or untagged.
             Without it the check only reports.
  PATH       limit the check to these files or directories.
"""

import argparse
import collections
import json
import os
import re
import subprocess
import sys

SOURCE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
LEDGER = os.path.join(SOURCE_DIR, 'docs', 'embedpdf', 'fork-ledger.md')
ALLOWLIST = os.path.join(SOURCE_DIR, 'scripts', 'embedpdf-runtime',
                         'fork-tags-allowlist.txt')

SOURCE_EXTENSIONS = ('.c', '.cc', '.cpp', '.h', '.mm')
LOOKBACK_LINES = 15
JOIN_GAP_LINES = 3
TAG = re.compile(r'EmbedPDF\(([a-z0-9][a-z0-9-]*(?:, [a-z0-9][a-z0-9-]*)*)\)')
RULE_HEADING = re.compile(r'^### ([a-z0-9][a-z0-9-]*)\s*$')
HUNK = re.compile(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@')
FAILING = ('unknown-rule', 'needs-rule', 'untagged')


def git(*args):
  return subprocess.run(['git', '-C', SOURCE_DIR, *args],
                        check=True,
                        capture_output=True,
                        text=True,
                        errors='replace').stdout


def read_rules():
  if not os.path.exists(LEDGER):
    return set()
  with open(LEDGER, encoding='utf-8') as f:
    return {m.group(1) for m in map(RULE_HEADING.match, f) if m}


def read_allowlist():
  allowed = {}
  if os.path.exists(ALLOWLIST):
    with open(ALLOWLIST, encoding='utf-8') as f:
      for line in f:
        line = line.strip()
        if line and not line.startswith('#'):
          path, _, reason = line.partition(' ')
          allowed[path] = reason.strip()
  return allowed


def is_checked(path):
  name = os.path.basename(path)
  return (path.endswith(SOURCE_EXTENSIONS) and
          not path.startswith('testing/') and 'test' not in name)


def changed_files(base, paths):
  out = git('diff', '--name-status', '--diff-filter=MR', base, '--', *paths)
  files = []
  for line in out.splitlines():
    fields = line.split('\t')
    path = fields[-1]  # a rename lists old and new; check the new path
    if is_checked(path):
      files.append(path)
  return files


def change_blocks(base, path):
  """Yields (first_new_line, last_new_line, removed, added) per block.

  Line numbers are 1-based lines of the file in the working tree. A block that only
  removes lines has last_new_line == first_new_line - 1.
  """
  diff = git('diff', '-U0', base, '--', path)
  hunks = []
  current = None
  for line in diff.splitlines():
    m = HUNK.match(line)
    if m:
      new_start = int(m.group(3))
      new_count = int(m.group(4)) if m.group(4) is not None else 1
      if new_count == 0:
        new_start += 1  # git names the line before a pure removal
      current = [new_start, new_start + new_count - 1, [], []]
      hunks.append(current)
    elif current is not None and line[:1] == '-' and not line.startswith('---'):
      current[2].append(line[1:])
    elif current is not None and line[:1] == '+' and not line.startswith('+++'):
      current[3].append(line[1:])

  block = None
  for hunk in hunks:
    if block and hunk[0] - block[1] <= JOIN_GAP_LINES + 1:
      block[1] = max(block[1], hunk[1])
      block[2] += hunk[2]
      block[3] += hunk[3]
    else:
      if block:
        yield tuple(block)
      block = list(hunk)
  if block:
    yield tuple(block)


def is_include_only(lines):
  texts = [l.strip() for l in lines if l.strip()]
  return bool(texts) and all(t.startswith('#include') for t in texts)


COMMENT_LINE = re.compile(r'^(//|/\*|\*(\s|/|$))')


def is_comment_only(lines):
  # A bare `*` starts a comment line only in a block comment's body (`* text`,
  # `*/`); `*buf << ...` is code.
  texts = [l.strip() for l in lines if l.strip()]
  return all(COMMENT_LINE.match(t) for t in texts)


def classify(window, removed, added, rules):
  changed = removed + added
  if is_include_only(changed) or is_comment_only(changed):
    return 'exempt', []
  named = [rule for tag in TAG.findall(window) for rule in tag.split(', ')]
  if named:
    unknown = [r for r in named if r not in rules]
    return ('unknown-rule' if unknown and len(unknown) == len(named) else
            'tagged'), named
  if 'EmbedPDF' in window:
    return 'needs-rule', []
  return 'untagged', []


def check(base, paths):
  rules = read_rules()
  allowed = read_allowlist()
  results = []
  for path in changed_files(base, paths):
    with open(os.path.join(SOURCE_DIR, path), encoding='utf-8',
              errors='replace') as f:
      lines = f.read().splitlines()
    for first, last, removed, added in change_blocks(base, path):
      if path in allowed:
        status, named = 'exempt', []
      else:
        top = max(0, first - 1 - LOOKBACK_LINES)
        window = '\n'.join(lines[top:max(last, first - 1)])
        status, named = classify(window, removed, added, rules)
      results.append({
          'path': path,
          'line': first,
          'status': status,
          'rules': named,
          'removed': len(removed),
          'added': len(added),
      })
  return rules, results


def default_base():
  # A fetched upstream/main first (see upstream-preview.py), then the mirror.
  for ref in ('upstream/main', 'pdfium/main', 'origin/pdfium/main'):
    try:
      return git('merge-base', 'HEAD', ref).strip()
    except subprocess.CalledProcessError:
      continue
  sys.exit('No upstream ref: fetch upstream/main or pass --base.')


def print_text(base, rules, results):
  info = git('log', '-1', '--format=%h %ad %s', '--date=short', base).strip()
  print(f'Upstream base: {info}')
  print(f'Rules in the ledger: {len(rules)}')
  counts = collections.Counter(r['status'] for r in results)
  print(f'Change blocks: {len(results)}')
  for status in ('tagged', 'exempt') + FAILING:
    print(f'  {status:13s} {counts[status]}')

  failing = [r for r in results if r['status'] in FAILING]
  if not failing:
    return
  print('\nBlocks to tag, by file:')
  by_file = collections.defaultdict(list)
  for r in failing:
    by_file[r['path']].append(r)
  for path in sorted(by_file, key=lambda p: (-len(by_file[p]), p)):
    entries = ', '.join(
        f"{r['line']}" + ('' if r['status'] == 'untagged' else
                          f" ({r['status']})") for r in by_file[path])
    print(f'  {path}: {entries}')


def main():
  parser = argparse.ArgumentParser(
      description=__doc__.split('\n\n')[0],
      formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument('--base')
  parser.add_argument('--strict', action='store_true')
  parser.add_argument('--format', choices=('text', 'json'), default='text')
  parser.add_argument('paths', nargs='*')
  args = parser.parse_args()

  base = git('rev-parse', args.base).strip() if args.base else default_base()
  rules, results = check(base, args.paths)
  if args.format == 'json':
    json.dump({'base': base, 'rules': sorted(rules), 'blocks': results},
              sys.stdout,
              indent=2)
    print()
  else:
    print_text(base, rules, results)

  if args.strict and any(r['status'] in FAILING for r in results):
    sys.exit(1)


if __name__ == '__main__':
  main()
