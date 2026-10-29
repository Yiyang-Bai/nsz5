#!/usr/bin/env bash
# Compress, decompress, and SHA-256-check every bundled sample.
# Writes archives under examples/out/ (gitignored). Does not download anything.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

if [[ ! -f work/libnsz.so ]]; then
    make lib
fi

if [[ -x .venv/bin/python ]]; then
    py=.venv/bin/python
elif [[ -x venv/bin/python ]]; then
    py=venv/bin/python
else
    py=python3
fi

out=examples/out
mkdir -p "$out"

for pod5 in examples/samples/*.pod5; do
    name=$(basename "$pod5" .pod5)
    echo
    echo "======== $name ========"
    "$py" python/nsz5.py compress "$pod5" "$out/$name.nsz5" --threads 2
    "$py" python/nsz5.py decompress "$out/$name.nsz5" "$out/$name.rebuilt.pod5" --threads 2
    "$py" python/nsz5.py verify "$pod5" "$out/$name.rebuilt.pod5"
done

echo
echo "Archives and rebuilt POD5 files are in $out/"
