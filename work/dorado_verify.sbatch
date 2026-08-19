#!/bin/bash
#SBATCH -J dorado-verify
#SBATCH -p scu-gpu
#SBATCH --gres=gpu:1
#SBATCH -c 8
#SBATCH --mem=64G
#SBATCH -t 6:00:00
#SBATCH -o /home/fs01/yib4003/nsz5/work/dorado_verify.%j.out

# Dorado double-run verification:
#   run 1: original POD5
#   run 2: original POD5 again (nondeterminism control)
#   run 3: nsz5-rebuilt POD5
# Since original and rebuilt are SHA-256 identical, run 3 must behave
# exactly like a rerun on the original. Outputs are canonicalized (records
# sorted by read id) before hashing.

set -euo pipefail
cd /home/fs01/yib4003/nsz5

DORADO=tools/dorado/dorado-2.1.1-linux-x64/bin/dorado
MODEL_DIR=work/dorado_models
MODEL=$(ls -d ${MODEL_DIR}/rna004*hac* | head -1)
echo "using model: ${MODEL}"
nvidia-smi -L

${DORADO} basecaller --emit-fastq "${MODEL}" data/rna004_control_rep1.pod5 > work/bc_orig1.fastq
${DORADO} basecaller --emit-fastq "${MODEL}" data/rna004_control_rep1.pod5 > work/bc_orig2.fastq
${DORADO} basecaller --emit-fastq "${MODEL}" work/rna004_rebuilt.pod5     > work/bc_rebuilt.fastq

echo "=== canonicalized hashes (records sorted by read id) ==="
for f in bc_orig1 bc_orig2 bc_rebuilt; do
    h=$(paste - - - - < work/${f}.fastq | sort | md5sum | cut -d' ' -f1)
    echo "${f}: ${h}"
done
echo "DORADO_VERIFY_DONE"
