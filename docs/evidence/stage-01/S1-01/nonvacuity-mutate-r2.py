"""
Deliberately re-break log.c for the S1-01 non-vacuity check, second round.

Round 1 (nonvacuity-mutate.py) covered audit B1 and B2. This round covers the two
blocking findings of the second audit, so their regression tests are shown to
fail against the broken code instead of passing vacuously:

  B1-R mutation: only '"' opens a quoted value again, which is the old scan that
                 left the body of password='alpha beta' in the sink.
  B3   mutation: the suffix test runs on the truncated normalised key again,
                 which is the old 63-character truncation that hid "_token" at
                 the end of a long field name.

Usage: python3 nonvacuity-mutate-r2.py <path-to-log.c>
"""
import sys

path = sys.argv[1]
text = open(path, encoding="utf-8").read()

# B1-R mutation: single quotes stop opening a quoted value.
old = """                if (cursor < length && (input[cursor] == '"' || input[cursor] == '\\'')) {"""
new = """                if (cursor < length && input[cursor] == '"') {"""
count = text.count(old)
if count != 1:
    raise SystemExit("B1R anchor count=%d, expected 1" % count)
text = text.replace(old, new)

# B3 mutation: suffix matching on the truncated normalised copy (old behaviour).
old = "        if (key_ends_with_n(key, length, k_sensitive_suffixes[i])) {"
new = "        if (key_ends_with_n(normalized, strlen(normalized), k_sensitive_suffixes[i])) {"
count = text.count(old)
if count != 1:
    raise SystemExit("B3 anchor count=%d, expected 1" % count)
text = text.replace(old, new)

open(path, "w", encoding="utf-8").write(text)
print("MUTATION_APPLIED")
