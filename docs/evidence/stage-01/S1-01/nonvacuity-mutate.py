"""
Deliberately re-break log.c for the S1-01 non-vacuity check.

Used by the evidence run in docs/evidence/stage-01/S1-01/nonvacuity-check.log:
the new B1/B2 regression tests must FAIL against the patched copy, otherwise they
do not actually guard the fix.

  B1 mutation: is_credential_char() treats '.' as a terminator again, which is the
               old word-based scan that leaked "part2.part3" after "[redacted]".
  B2 mutation: scrub_stream() stops masking after 16 spans again, which is the old
               fixed budget that let the 17th secret reach the sink.

Usage: python3 nonvacuity-mutate.py <path-to-log.c>
"""
import sys

path = sys.argv[1]
text = open(path, encoding="utf-8").read()

# B1 mutation: a credential stops at a dot again.
old = "        case '\\\\':\n            return false;"
new = "        case '\\\\':\n        case '.':\n            return false;"
count = text.count(old)
if count != 1:
    raise SystemExit("B1 anchor count=%d, expected 1" % count)
text = text.replace(old, new)

# B2 mutation: reintroduce the 16-span budget.
anchor = "    while (i < length) {\n        if (!is_word_char(input[i])"
count = text.count(anchor)
if count != 1:
    raise SystemExit("B2 anchor count=%d, expected 1" % count)
budget = (
    "    while (i < length) {\n"
    "        if (masked >= 16u) {\n"
    "            writer_putc(writer, input[i]);\n"
    "            i += 1u;\n"
    "            continue;\n"
    "        }\n"
    "        if (!is_word_char(input[i])"
)
text = text.replace(anchor, budget)

open(path, "w", encoding="utf-8").write(text)
print("MUTATION_APPLIED")
