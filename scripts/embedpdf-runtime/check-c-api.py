#!/usr/bin/env python3
# Copyright 2026 CloudPDF LTD
# SPDX-License-Identifier: Apache-2.0

"""Check that fpdfsdk/epdf_c_api_test.c lists every EPDF* function.

epdf_c_api_test.c includes our public headers from C and takes the address of
every function they declare, so a header that only compiles as C++, or a
declaration with no definition behind it, fails the embedder tests
(epdf.CApiTest). It only catches what it lists; this keeps the list complete.

It fails when:
  - a public header declares an EPDF* function the test doesn't list;
  - the test lists a function no public header declares (renamed or gone);
  - a header's functions aren't listed in alphabetical order, or a header our
    functions live in isn't included.

Run it from the fork's root:
  check-c-api.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(
    __file__))))
TEST = os.path.join(ROOT, 'fpdfsdk', 'epdf_c_api_test.c')
PUBLIC = os.path.join(ROOT, 'public')


def strip_comments(text):
  text = re.sub(r'//[^\n]*', '', text)
  return re.sub(r'/\*.*?\*/', '', text, flags=re.S)


def declared():
  """EPDF* functions by the public header that declares them."""
  by_header = {}
  for name in sorted(os.listdir(PUBLIC)):
    if not name.endswith('.h'):
      continue
    with open(os.path.join(PUBLIC, name)) as f:
      text = strip_comments(f.read())
    functions = [
        n for n in re.findall(r'FPDF_CALLCONV\s+(\w+)\s*\(', text)
        if n.startswith('EPDF')
    ]
    if functions:
      by_header[name] = functions
  return by_header


def listed():
  """The test's sections: header comment -> CHK names, in order."""
  with open(TEST) as f:
    lines = f.read().split('\n')
  sections, current = {}, None
  includes = set(re.findall(r'#include "public/([\w.]+)"', '\n'.join(lines)))
  for line in lines:
    m = re.match(r'\s*// ([\w.]+\.h)$', line)
    if m:
      current = m.group(1)
      sections[current] = []
      continue
    m = re.match(r'\s*CHK\((\w+)\);', line)
    if m:
      sections.setdefault(current, []).append(m.group(1))
  return sections, includes


def main():
  want = declared()
  have, includes = listed()
  problems = []
  for header, functions in sorted(want.items()):
    got = have.get(header, [])
    for name in sorted(set(functions) - set(got)):
      problems.append(f'{header}: {name} is not listed')
    if got != sorted(got):
      problems.append(f'{header}: the section is not in alphabetical order')
    if header not in includes:
      problems.append(f'{header}: not included')
  every = {n for functions in want.values() for n in functions}
  for header, got in sorted(have.items()):
    for name in got:
      if name not in every:
        problems.append(f'{header}: {name} is listed but no header declares it')
  for p in problems:
    print(p)
  if problems:
    print(f'\n{len(problems)} problem(s) in {os.path.relpath(TEST, ROOT)}')
    return 1
  print(f'ok: {len(every)} functions in {len(want)} headers')
  return 0


if __name__ == '__main__':
  sys.exit(main())
