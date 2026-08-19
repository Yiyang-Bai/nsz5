#!/bin/bash
#SBATCH -J sigcomp-bench
#SBATCH -p scu-cpu
#SBATCH -c 32
#SBATCH --mem=250G
#SBATCH -t 12:00:00
#SBATCH -o /home/fs01/yib4003/nsz5/work/sigcomp_bench.%j.out

# Reproduction of the ex-zd paper's Fig 1 benchmark (Gamaarachchi et al.,
# Genome Res 2025) on the EXACT same dataset: HG002-Prom5K Chr22 subset
# (flowcell PGXXSX240041, downloaded from s3://gtgseq), using the same
# slow5tools v1.3.0 and the same metric (du bytes; ratio vs uncompressed),
# with POD5/VBZ and nsz5 added to the comparison.

set -uo pipefail
cd /home/fs01/yib4003/nsz5
ST=tools/slow5tools-v1.3.0/slow5tools
IN=data/hg002_prom5k_chr22.blow5
WORK=work/sigcomp
mkdir -p ${WORK}

echo "=== input (as-downloaded blow5: zlib,svb-zd) ==="
du -b ${IN}

run_variant () {
    local name=$1 c=$2 s=$3
    local out=${WORK}/${name}.blow5
    local t0=$(date +%s)
    ${ST} view -t 32 -c ${c} -s ${s} --to blow5 ${IN} -o ${out} \
        || { echo "VARIANT ${name} FAILED"; rm -f ${out}; return; }
    local t1=$(date +%s)
    echo "SIZE ${name} $(du -b ${out} | cut -f1) time $((t1-t0))s"
    rm -f ${out}
}

run_variant none_none  none none
run_variant zlib_svb-zd zlib svb-zd
run_variant zstd_svb-zd zstd svb-zd
run_variant zlib_ex-zd  zlib ex-zd
run_variant zstd_ex-zd  zstd ex-zd

echo "=== POD5 (native VBZ) via blue-crab ==="
t0=$(date +%s)
venv/bin/blue-crab s2p ${IN} -o ${WORK}/chr22.pod5
t1=$(date +%s)
echo "SIZE pod5_vbz $(du -b ${WORK}/chr22.pod5 | cut -f1) time $((t1-t0))s"

echo "=== nsz5 (this work, R10 profile + held-out prior) ==="
t0=$(date +%s)
venv/bin/python python/nsz5.py compress ${WORK}/chr22.pod5 ${WORK}/chr22.nsz5 \
    --prior models/prior_r10.bin
t1=$(date +%s)
echo "SIZE nsz5 $(du -b ${WORK}/chr22.nsz5 | cut -f1) time $((t1-t0))s"

echo "=== nsz5 byte-exactness verification on the paper's data ==="
venv/bin/python python/nsz5.py decompress ${WORK}/chr22.nsz5 ${WORK}/chr22_rebuilt.pod5
venv/bin/python python/nsz5.py verify ${WORK}/chr22.pod5 ${WORK}/chr22_rebuilt.pod5
rm -f ${WORK}/chr22_rebuilt.pod5

echo "BENCH_ALL_DONE"
