# Large-dataset compression benchmark

Protocol: slow5tools v1.3.0 (32 threads) + nsz5 (32 threads). Sizes from `du -b`. Savings relative to POD5 (VBZ).

| Dataset | Chemistry | Samples | Uncompressed | VBZ (POD5) | ex-zd zstd | nsz5 | nsz vs VBZ | nsz vs ex-zd | Encode | Decode |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| HG002 Chr22 | R10.4.1 DNA | 32.3 B | 64.7 GB | 28.2 GB | 27.5 GB | 27.2 GB | **3.5%** | 1.2% | 48 Ms/s | 29 Ms/s |
| HEPG2 RNA002 | R9 RNA002 | 9.5 B | 19.0 GB | 8.8 GB | 8.6 GB | 8.2 GB | **6.8%** | 5.1% | 49 Ms/s | 70 Ms/s |
| RNA004 full | RNA004 | 69.5 B | 140.6 GB | 63.1 GB | 62.4 GB | 58.1 GB | **7.9%** | 6.8% | 37 Ms/s | 78 Ms/s |

## All methods (bytes)

| Dataset | Uncomp | zlib svb-zd | zstd svb-zd | zlib ex-zd | zstd ex-zd | VBZ | nsz5 |
|---|---:|---:|---:|---:|---:|---:|---:|
| HG002 Chr22 | 64.69 GB | 28.47 GB | 28.23 GB | 27.74 GB | 27.50 GB | 28.15 GB | 27.17 GB |
| HEPG2 RNA002 | 19.01 GB | 8.85 GB | 8.82 GB | 8.65 GB | 8.63 GB | 8.78 GB | 8.19 GB |
| RNA004 full | 140.55 GB | 63.66 GB | 63.43 GB | 62.47 GB | 62.40 GB | 63.15 GB | 58.15 GB |
