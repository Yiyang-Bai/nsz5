#!/bin/bash
#SBATCH -J nsz-rna004-dec
#SBATCH -p scu-cpu
#SBATCH -c 32
#SBATCH --mem=64G
#SBATCH -t 10:00:00
#SBATCH -o /home/fs01/yib4003/nsz5/work/rna004_decompress.%j.out

set -euo pipefail
cd /home/fs01/yib4003/nsz5
W=work/bench_rna004
POD5=data/rna004_full.pod5

echo "SIZE nsz5 $(du -b ${W}/signal.nsz5 | cut -f1)"
echo "SIZE pod5_vbz $(du -b ${POD5} | cut -f1)"

t0=$(date +%s)
venv/bin/python python/nsz5.py decompress ${W}/signal.nsz5 ${W}/rebuilt.pod5 --threads 32
echo "DECOMPRESS_SECONDS $(($(date +%s)-t0))"

venv/bin/python python/nsz5.py verify ${POD5} ${W}/rebuilt.pod5
echo "RNA004_DECOMPRESS_DONE"
