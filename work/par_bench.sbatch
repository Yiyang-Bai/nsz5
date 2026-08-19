#!/bin/bash
#SBATCH -J nsz-par-bench
#SBATCH -p scu-cpu
#SBATCH -c 32
#SBATCH --mem=250G
#SBATCH -t 2:00:00
#SBATCH -o /home/fs01/yib4003/nsz5/work/par_bench.%j.out

# S1 acceptance at scale: parallel-compress the ex-zd paper's chr22 POD5
# (32.3 B samples; serial baseline was 5071 s) and verify SHA-256.

set -uo pipefail
cd /home/fs01/yib4003/nsz5

t0=$(date +%s)
venv/bin/python python/nsz5.py compress work/sigcomp/chr22.pod5 \
    work/sigcomp/chr22_par.nsz5 --prior models/prior_r10.bin --threads 32
t1=$(date +%s)
echo "PARALLEL_COMPRESS_SECONDS $((t1-t0))"
du -b work/sigcomp/chr22_par.nsz5

venv/bin/python python/nsz5.py decompress work/sigcomp/chr22_par.nsz5 \
    work/sigcomp/chr22_par_rebuilt.pod5
venv/bin/python python/nsz5.py verify work/sigcomp/chr22.pod5 \
    work/sigcomp/chr22_par_rebuilt.pod5
rm -f work/sigcomp/chr22_par_rebuilt.pod5
echo "PAR_BENCH_DONE"
