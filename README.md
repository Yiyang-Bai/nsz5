# nsz5

Lossless compression for Oxford Nanopore POD5 files. Decompression
rebuilds the original file byte for byte.

nsz5 把 Oxford Nanopore 的 POD5 原始信号再压一层。解压得到的 POD5 和输入
逐字节相同（SHA-256 相同），所以后面的 basecalling 不会因为换了压缩格式而改变。
`.nsz5` 是归档格式：分析之前要先解压回 POD5。

信号本身走一套针对纳米孔电流的编码器（固定阶数线性预测、幅度分层、自适应区间编码）。
POD5 里除信号以外的 Arrow 元数据原样保留。某个信号块如果用当前的 zstd 版本
还原不了原字节，就整块照抄，文件只会变大，不会变错。

## 先跑通仓库里的样例

仓库里带了四份很小的 POD5，每种公开数据各切了一小段，用来确认安装和命令。
这几份文件只有几条到几十条 read，压缩率没有参考价值。完整数据上的比例见文末。

依赖：GCC（支持 OpenMP）、Python 3.10+。

```bash
git clone https://github.com/Yiyang-Bai/nsz5.git
cd nsz5
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
make lib
bash examples/quickstart.sh
```

`requirements.txt` 里压缩和解压真正需要的是 `pod5`、`pyarrow`、`numpy`、`zstandard`。
`blue-crab` 只在把 BLOW5 转成 POD5 时用，`pyflac` 只在和 FLAC 对比时用。

`examples/quickstart.sh` 会依次压缩、解压、比对这四份样例，产物写在
`examples/out/`（不进 git）。每一份都应该打印 `IDENTICAL`。

手动跑一份的话：

```bash
python python/nsz5.py compress \
    examples/samples/rna004_control.pod5 \
    examples/out/rna004_control.nsz5

python python/nsz5.py decompress \
    examples/out/rna004_control.nsz5 \
    examples/out/rna004_control.rebuilt.pod5

python python/nsz5.py verify \
    examples/samples/rna004_control.pod5 \
    examples/out/rna004_control.rebuilt.pod5
```

`verify` 比对两个文件的 SHA-256。相同打印 `IDENTICAL`，不同打印 `DIFFERENT`。

## 命令

程序是 `python/nsz5.py`，四个子命令：

```text
python python/nsz5.py compress   输入.pod5  输出.nsz5
python python/nsz5.py decompress 输入.nsz5  还原.pod5
python python/nsz5.py verify     甲.pod5    乙.pod5
python python/nsz5.py bench      输入.pod5
```

压缩时先验默认是 `auto`：测序试剂盒名字里带 `rna` 就用 `models/prior.bin`
（R9 直接 RNA 和 RNA004），否则用 `models/prior_r10.bin`（R10.4.1 DNA）。
也可以自己指定，或者关掉：

```bash
python python/nsz5.py compress in.pod5 out.nsz5 --prior models/prior.bin
python python/nsz5.py compress in.pod5 out.nsz5 --prior none
```

`--threads` 是进程数。`0` 表示按机器核数自动取，最多 32。默认是 1。
压缩和解压都可以加。块与块之间独立，多进程的结果和单进程逐字节相同。

```bash
python python/nsz5.py compress   in.pod5 out.nsz5 --threads 8
python python/nsz5.py decompress out.nsz5 rebuilt.pod5 --threads 8
```

`bench` 只比较信号载荷和 POD5 自带的 VBZ，不写归档文件：

```bash
python python/nsz5.py bench examples/samples/hct116_directrna_r9.pod5
```

大文件会把整个 POD5 读进内存，压缩时大约需要两倍文件大小的内存。

## 仓库里的样例

样例是从公开文件最前面取出若干条 read，再用 POD5 重写的小文件，不是原文件的字节前缀。
一条 read 有时会拆成多块信号，所以块数可以多于 read 数。

| 文件 | 数据 | read 数 | 自动选用的先验 |
|---|---|---|---|
| `examples/samples/hct116_directrna_r9.pod5` | SG-NEx Hct116 直接 RNA，R9.4.1，SQK-RNA001，约 3 kHz | 24 | `prior.bin` |
| `examples/samples/hepg2_directrna_r9.pod5` | SG-NEx HepG2 直接 RNA，R9.4.1，SQK-RNA001，约 3 kHz | 16 | `prior.bin` |
| `examples/samples/rna004_control.pod5` | ONT RNA004 修饰对照，SQK-RNA004，4 kHz | 24 | `prior.bin` |
| `examples/samples/hg002_r10_dna.pod5` | GIAB HG002，R10.4.1 DNA，SQK-LSK114，5 kHz | 3 | `prior_r10.bin` |

DNA 的 read 比直接 RNA 长，所以 HG002 只留了 3 条，文件大约 900 KB。

## 下载完整的公开数据

下面这些对象和本仓库实验用的是同一份文件（按字节大小对过）。
不需要 AWS 账号。装了 [AWS CLI](https://aws.amazon.com/cli/) 之后，命令里的
`--no-sign-request` 就是匿名下载。也可以用浏览器打开 HTTPS 链接。

BLOW5 要先转成 POD5，nsz5 才读得了：

```bash
pip install blue-crab
blue-crab s2p 输入.blow5 -o 输出.pod5
```

### R9.4.1 直接 RNA（SG-NEx）

目录和说明：[GoekeLab/sg-nex-data](https://github.com/GoekeLab/sg-nex-data)，
AWS 开放数据登记 [sgnex](https://registry.opendata.aws/sgnex/)。

Hct116，replicate 6 run 1（88 MB，样例就是从这里切的）：

```bash
aws s3 cp --no-sign-request \
  s3://sg-nex-data-blow5/SGNex_Hct116_directRNA_replicate6_run1/SGNex_Hct116_directRNA_replicate6_run1.blow5 \
  .
```

HTTPS：<https://sg-nex-data-blow5.s3.ap-southeast-1.amazonaws.com/SGNex_Hct116_directRNA_replicate6_run1/SGNex_Hct116_directRNA_replicate6_run1.blow5>

同一细胞系、更大的 replicate 2 run 2（630 MB）：

```bash
aws s3 cp --no-sign-request \
  s3://sg-nex-data-blow5/SGNex_Hct116_directRNA_replicate2_run2/SGNex_Hct116_directRNA_replicate2_run2.blow5 \
  .
```

HepG2，replicate 3 run 1（8.3 GB，试剂盒是 SQK-RNA001）：

```bash
aws s3 cp --no-sign-request \
  s3://sg-nex-data-blow5/SGNex_HepG2_directRNA_replicate3_run1/SGNex_HepG2_directRNA_replicate3_run1.blow5 \
  .
```

HTTPS：<https://sg-nex-data-blow5.s3.ap-southeast-1.amazonaws.com/SGNex_HepG2_directRNA_replicate3_run1/SGNex_HepG2_directRNA_replicate3_run1.blow5>

### RNA004（ONT 开放数据）

说明：[RNA modified-base benchmarking](https://epi2me.nanoporetech.com/rna-mod-validation-data/)。
桶的登记页：[ont-open-data](https://registry.opendata.aws/ont-open-data/)。

先下 348 MB 的子集就够把流程跑通。样例的 24 条 read 来自这个文件：

```bash
aws s3 cp --no-sign-request \
  s3://ont-open-data/rna-modbase-validation_2025.03/subset/control_rep1.pod5 \
  rna004_control_rep1.pod5
```

HTTPS：<https://ont-open-data.s3.eu-west-1.amazonaws.com/rna-modbase-validation_2025.03/subset/control_rep1.pod5>

全量对照（约 59 GB）在同一发布的 `full/` 下面：

```bash
aws s3 cp --no-sign-request \
  s3://ont-open-data/rna-modbase-validation_2025.03/full/control_rep1.pod5 \
  rna004_control_rep1_full.pod5
```

这是 POD5，下载后可以直接交给 `nsz5.py`，不用转格式。

### R10.4.1 DNA，5 kHz（GIAB HG002）

2025.01 发布说明：[Genome in a Bottle Data Release 2025.01](https://epi2me.nanoporetech.com/giab-2025.01/)。
HG002 的一张芯片是 `PAW70337`，里面有一百多个 POD5。下面这一个是 1.88 GB，
样例的 3 条 read 来自它，拿来试命令足够：

```bash
aws s3 cp --no-sign-request \
  s3://ont-open-data/giab_2025.01/flowcells/HG002/PAW70337/pod5/PAW70337_66b2eea5_de8117b1_10.pod5 \
  hg002_r10.pod5
```

HTTPS：<https://ont-open-data.s3.eu-west-1.amazonaws.com/giab_2025.01/flowcells/HG002/PAW70337/pod5/PAW70337_66b2eea5_de8117b1_10.pod5>

整张芯片：

```bash
aws s3 sync --no-sign-request \
  s3://ont-open-data/giab_2025.01/flowcells/HG002/PAW70337/pod5/ \
  hg002_paw70337/
```

### 和 ex-zd 对比时用的 chr22 子集

Garvan 的 gtgseq 桶，[数据说明](https://gentechgp.github.io/gtgseq/docs/data.html)，
登记页 [gtgseq](https://registry.opendata.aws/gtgseq/)。
slow5tools 的数据页也列了这份 chr22 子集：<https://hasindu2008.github.io/slow5tools/datasets.html>。

HG002 / NA24385，flow cell `PGXXSX240041`，R10.4.1，5 kHz，比对到 chr22 的 read
（27 GB BLOW5）：

```bash
aws s3 cp --no-sign-request \
  s3://gtgseq/misc/sub/ont-r10-5khz-dna/PGXXSX240041_reads_chr22.blow5 \
  .
```

HTTPS：<https://gtgseq.s3.us-west-2.amazonaws.com/misc/sub/ont-r10-5khz-dna/PGXXSX240041_reads_chr22.blow5>

同一张芯片的全量信号（BLOW5，ENA 登录号 [ERR12997168](https://www.ebi.ac.uk/ena/browser/view/ERR12997168)）：

```bash
aws s3 cp --no-sign-request \
  s3://gtgseq/ont-r10-5khz-dna/NA24385_2/raw/PGXXSX240041_reads.blow5 \
  .
```

转成 POD5 之后用 R10 先验：

```bash
blue-crab s2p PGXXSX240041_reads_chr22.blow5 -o chr22.pod5
python python/nsz5.py compress chr22.pod5 chr22.nsz5 --prior models/prior_r10.bin --threads 8
```

## 压缩一条数据时实际发生的事

POD5 被当成字节流。Arrow 元数据打进一个 zstd 骨架；每一块 VBZ 信号解成 int16，
再用 nsz 重压。解压时按原来的偏移把 VBZ 块拼回去，并核对文件头里保存的 SHA-256。

信号编码器，对一块 int16：

```text
int16 信号
  -> 8 阶线性预测（Q14 整数；R9/RNA 用拟合系数，R10 DNA 用一阶差分）
  -> 自适应偏置修正
  -> 残差做 zigzag，再拆成幅度类和尾数
  -> 按波动和“意外程度”选 256 个上下文
  -> 用预训练先验初始化的自适应二元区间编码
```

两份先验都在 `models/`，各约 21 KB，和预测器配置绑在一起。块头记下用的是哪一版，
对不上时解码直接报错，不会猜。

一块如果压完不比原始 int16 小，就原样存放。均匀随机输入最多多 28 字节的块头。

## 完整数据上的结果

比例都是相对同一份数据上的 VBZ。样例文件太短，不要用它们的 `bench` 输出代替下表。

信号层，每种化学各 2000 万个采样点：

| 编码器 | R9 直接 RNA | RNA004 | R10.4.1 DNA | 编码速度 |
|---|---|---|---|---|
| nsz5 | 6.432 bit/样点（比 VBZ 小 4.0%） | 6.570（小 5.9%） | 6.729（小 2.8%） | 约 11–12 M 样点/秒 |
| FLAC -8 | 6.514（小 2.8%） | 6.643（小 4.9%） | 6.855（小 1.0%） | 5–26 M 样点/秒 |
| VBZ（POD5 默认） | 6.702 | 6.983 | 6.923 | 约 500 M 样点/秒 |
| 未压缩 int16 | 16 | 16 | 16 | — |

整文件来回，解压结果的 SHA-256 与原 POD5 相同：

| 文件 | 化学 | 体积 | 比原 POD5 小 | 原样回退的块 |
|---|---|---|---|---|
| SG-NEx Hct116 直接 RNA | R9.4.1 / SQK-RNA001 | 92 MB | 4.22% | 0 / 7897 |
| ONT RNA004 对照子集 | RNA004 | 364 MB | 7.79% | 0 / 60053 |
| GIAB HG002 的一个 POD5 | R10.4.1 DNA，5 kHz | 1.88 GB | 2.83% | 49 / 26300 |

在 ex-zd 论文的同一份 chr22 子集上（32.3 G 个样点，slow5tools 1.3.0，用 `du` 的字节数，
相对未压缩 BLOW5）：

| 方法 | 字节 | 相对未压缩 BLOW5 |
|---|---|---|
| nsz5 | 27,339,081,585 | 2.366 |
| ex-zd zstd | 27,503,933,718 | 2.352 |
| POD5 / VBZ | 28,154,437,312 | 2.298 |
| 未压缩 BLOW5 | 64,685,036,199 | 1.000 |

32 线程时，这份 chr22 数据压缩从 5071 秒降到 442 秒，解压从 2648 秒降到 393 秒，
SHA-256 仍然一致。

## 目录

```text
src/                 C99 编码器（nsz.c、预测器、区间编码、上下文）
python/nsz5.py       POD5 归档命令：compress / decompress / verify / bench
python/libnsz.py     对 work/libnsz.so 的 ctypes 绑定
models/prior.bin     R9 直接 RNA 和 RNA004 的先验
models/prior_r10.bin R10.4.1 DNA 的先验
examples/samples/    上面四份小 POD5
examples/quickstart.sh
tools/extract_signal.py   POD5 信号导出成小端 int16，给分析脚本用
tools/slice_pod5.py       从 POD5 里取出前 N 条 read，样例就是这样切的
analysis/benchmark.py     和 FLAC、zstd、xz、bzip2 比信号层比特率
tests/               make test，带 AddressSanitizer 和 UndefinedBehaviorSanitizer
```

只想压信号、不包一层 POD5 容器时，链接 `work/libnsz.so`，接口在 `src/nsz.h`。

## 测试

```bash
make test
```

覆盖 zigzag、预测器同步、区间编码、幅度分解、上下文、真实信号往返、
损坏输入不会被静默解成错误数据，以及先验版本不匹配时拒绝解码。

## 限制

- 编码器单线程大约十几个 M 样点/秒，比 VBZ 慢。块是独立的，用 `--threads` 摊到多核。
- R10 DNA 在一阶差分之后接近白噪声，相对 VBZ 的增益比 RNA 小。
- 归档要先解压才能交给 Dorado 或其他只读 POD5 的程序。
- 信号块外层的 zstd 帧和写文件时的 libzstd 版本有关。本机如果准备了
  `work/zstd_env_<版本>/lib/libzstd.so.1`（1.5.6、1.5.5、1.5.2、1.5.0、1.4.9），
  压缩时会自动试这些版本。一个都对不上的块会原样保存，解压结果仍然逐字节相同。
