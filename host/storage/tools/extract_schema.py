#!/usr/bin/env python3
"""Regenerate host/storage/src/schema_records.json from the contract schema.

Why this file exists
--------------------
host/storage/src/schema_records.json is the frozen subset of
contracts/internal-data.schema.json that the storage validator embeds at
configure time. It is NOT a hand-written copy: it is derived mechanically from
the contract, and the derivation used to live only in a throwaway script. Audit
N5 of S1-02 asked for the tool to be kept in the tree plus an automatic
contract-drift gate, which is what this file provides.

The subset is:
  * every collection name from x-collection-schemas, mapped to its record name;
  * the transitive $ref closure of those record names;
  * plus the seven repository port DTOs the storage module itself validates
    (RepoDocument, RepoCheck, RepoMutation, RepositoryChange, RepositoryQuery,
    RepositoryQueryResult, RepositoryRecoveryPage) and their closure.
description keys are dropped (they are prose, not validation), all object keys
are sorted, and the result is serialised compactly with one trailing newline.
That normalisation is what makes the output byte-reproducible.

Usage
-----
  python3 -B host/storage/tools/extract_schema.py           # rewrite the file
  python3 -B host/storage/tools/extract_schema.py --check   # verify, exit 1 on drift
"""
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
CONTRACT = ROOT / "contracts" / "internal-data.schema.json"
FROZEN = ROOT / "host" / "storage" / "src" / "schema_records.json"
SOURCE_LABEL = "contracts/internal-data.schema.json"

# Repository port DTOs validated by host/storage/src/repository.c. They are not
# reachable from any collection schema, so they are extra roots.
PORT_ROOTS = (
    "RepoDocument",
    "RepoCheck",
    "RepoMutation",
    "RepositoryChange",
    "RepositoryQuery",
    "RepositoryQueryResult",
    "RepositoryRecoveryPage",
)


def strip_description(value):
    """Recursively drop 'description' keys; keep everything else verbatim."""
    if isinstance(value, list):
        return [strip_description(item) for item in value]
    if isinstance(value, dict):
        return {k: strip_description(v) for k, v in value.items() if k != "description"}
    return value


def sort_keys(value):
    """Recursively sort object keys so serialisation is deterministic."""
    if isinstance(value, list):
        return [sort_keys(item) for item in value]
    if isinstance(value, dict):
        return {k: sort_keys(value[k]) for k in sorted(value)}
    return value


def iter_refs(value):
    """Yield every '#/$defs/<name>' target reachable from value."""
    if isinstance(value, list):
        for item in value:
            yield from iter_refs(item)
    elif isinstance(value, dict):
        for key, item in value.items():
            if key == "$ref" and isinstance(item, str) and item.startswith("#/$defs/"):
                yield item[len("#/$defs/"):]
            else:
                yield from iter_refs(item)


def build_subset(contract):
    defs = contract["$defs"]
    collections = strip_description(contract["x-collection-schemas"])

    needed = set(collections.values()) | set(PORT_ROOTS)
    frontier = list(needed)
    while frontier:
        name = frontier.pop()
        if name not in defs:
            raise SystemExit("schema drift: x-collection-schemas references unknown $defs entry %r" % name)
        for ref in iter_refs(defs[name]):
            if ref not in needed:
                needed.add(ref)
                frontier.append(ref)

    missing = sorted(name for name in needed if name not in defs)
    if missing:
        raise SystemExit("schema drift: unresolved $ref targets: %s" % ", ".join(missing))

    subset_defs = {name: strip_description(defs[name]) for name in sorted(needed)}
    payload = {"collections": collections, "defs": subset_defs, "source": SOURCE_LABEL}
    return json.dumps(sort_keys(payload), separators=(",", ":"), ensure_ascii=False) + "\n"


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify the frozen file instead of rewriting it")
    args = parser.parse_args(argv)

    # Binary I/O on purpose: text mode would translate \n to \r\n on Windows and
    # make the frozen file platform-dependent (the bytes are embedded verbatim
    # into the library, and the drift gate must be byte-exact on every host).
    contract = json.loads(CONTRACT.read_bytes().decode("utf-8"))
    expected = build_subset(contract)

    if args.check:
        if not FROZEN.exists():
            print("MISSING %s" % FROZEN.relative_to(ROOT))
            return 1
        actual = FROZEN.read_bytes().decode("utf-8")
        if actual != expected:
            print("DRIFT %s does not match %s" % (FROZEN.relative_to(ROOT), SOURCE_LABEL))
            print("      rerun: python3 -B host/storage/tools/extract_schema.py")
            return 1
        print("OK %s matches %s (%d bytes)" % (FROZEN.relative_to(ROOT), SOURCE_LABEL, len(expected.encode("utf-8"))))
        return 0

    FROZEN.write_bytes(expected.encode("utf-8"))
    print("WROTE %s (%d bytes)" % (FROZEN.relative_to(ROOT), len(expected.encode("utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))