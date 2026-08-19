#!/bin/bash
#SBATCH -J nsz-chem-bench
#SBATCH -p scu-cpu
#SBATCH -c 32
#SBATCH --mem=250G
#SBATCH -t 8:00:00
#SBATCH -o /home/fs01/yib4003/nsz5/work/chem_bench.%j.out

# Per-chemistry large-dataset benchmark, following the ex-zd paper protocol:
#   slow5tools v1.3.0 {zlib,zstd} x {svb-zd,ex-zd} + uncompressed baseline,
#   POD5 (native VBZ), and nsz5 (auto prior), all measured with du -b.
#
# Usage: sbatch chem_bench.sbatch <name> <input.{blow5|pod5}>

set -uo pipefail
NAME=$1
INPUT=$2
cd /home/fs01/yib4003/nsz5
ST=tools/slow5tools-v1.3.0/slow5tools
W=work/bench_${NAME}
mkdir -p ${W}

# normalize to both container forms
if [[ ${INPUT} == *.pod5 ]]; then
    POD5=${INPUT}
    BLOW5=${W}/signal.blow5
    venv/bin/blue-crab p2s ${INPUT} -o ${BLOW5} 2>&1 | tail -1
else
    BLOW5=${INPUT}
    POD5=${W}/signal.pod5
    venv/bin/blue-crab s2p ${INPUT} -o ${POD5} 2>&1 | tail -1
fi
echo "SIZE pod5_vbz $(du -b ${POD5} | cut -f1)"
echo "SIZE blow5_input $(du -b ${BLOW5} | cut -f1)"

run_variant () {
    local name=$1 c=$2 s=$3
    local out=${W}/${name}.blow5
    local t0=$(date +%s)
    ${ST} view -t 32 -c ${c} -s ${s} --to blow5 ${BLOW5} -o ${out} \
        || { echo "VARIANT ${name} FAILED"; rm -f ${out}; return; }
    echo "SIZE ${name} $(du -b ${out} | cut -f1) time $(($(date +%s)-t0))s"
    rm -f ${out}
}
run_variant none_none   none none
run_variant zlib_svb-zd zlib svb-zd
run_variant zstd_svb-zd zstd svb-zd
run_variant zlib_ex-zd  zlib ex-zd
run_variant zstd_ex-zd  zstd ex-zd

t0=$(date +%s)
venv/bin/python python/nsz5.py compress ${POD5} ${W}/signal.nsz5 --threads 32
echo "COMPRESS_SECONDS $(($(date +%s)-t0))"
echo "SIZE nsz5 $(du -b ${W}/signal.nsz5 | cut -f1)"

t0=$(date +%s)
venv/bin/python python/nsz5.py decompress ${W}/signal.nsz5 ${W}/rebuilt.pod5 --threads 32
echo "DECOMPRESS_SECONDS $(($(date +%s)-t0))"
venv/bin/python python/nsz5.py verify ${POD5} ${W}/rebuilt.pod5
rm -f ${W}/rebuilt.pod5
echo "CHEM_BENCH_DONE ${NAME}"
