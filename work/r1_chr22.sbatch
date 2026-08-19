#!/bin/bash
#SBATCH -J nsz-r1-chr22
#SBATCH -p scu-cpu
#SBATCH -c 32
#SBATCH --mem=250G
#SBATCH -t 3:00:00
#SBATCH -o /home/fs01/yib4003/nsz5/work/r1_chr22.%j.out
set -uo pipefail
cd /home/fs01/yib4003/nsz5
mkdir -p work/sigcomp
venv/bin/blue-crab s2p data/hg002_prom5k_chr22.blow5 -o work/sigcomp/chr22.pod5 2>&1 | tail -1
echo "SIZE pod5 $(du -b work/sigcomp/chr22.pod5 | cut -f1)"
t0=$(date +%s)
venv/bin/python python/nsz5.py compress work/sigcomp/chr22.pod5 work/sigcomp/chr22.nsz5 \
    --prior models/prior_r10.bin --threads 32
t1=$(date +%s)
echo "COMPRESS_SECONDS $((t1-t0))"
echo "SIZE nsz5_v2 $(du -b work/sigcomp/chr22.nsz5 | cut -f1)"
venv/bin/python python/nsz5.py decompress work/sigcomp/chr22.nsz5 work/sigcomp/chr22_v.pod5 --threads 32
venv/bin/python python/nsz5.py verify work/sigcomp/chr22.pod5 work/sigcomp/chr22_v.pod5
rm -f work/sigcomp/chr22_v.pod5
echo "R1_CHR22_DONE"
