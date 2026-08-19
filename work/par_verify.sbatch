#!/bin/bash
#SBATCH -J nsz-par-verify
#SBATCH -p scu-cpu
#SBATCH -c 32
#SBATCH --mem=250G
#SBATCH -t 1:00:00
#SBATCH -o /home/fs01/yib4003/nsz5/work/par_verify.%j.out
set -uo pipefail
cd /home/fs01/yib4003/nsz5
t0=$(date +%s)
venv/bin/python python/nsz5.py decompress work/sigcomp/chr22_par.nsz5 \
    work/sigcomp/chr22_v.pod5 --threads 32
t1=$(date +%s)
echo "PARALLEL_DECOMPRESS_SECONDS $((t1-t0))"
venv/bin/python python/nsz5.py verify work/sigcomp/chr22.pod5 work/sigcomp/chr22_v.pod5
rm -f work/sigcomp/chr22_v.pod5
echo "PAR_VERIFY_DONE"
