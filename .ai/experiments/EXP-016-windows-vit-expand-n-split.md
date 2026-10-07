# EXP-016：ViT 扩张只拆输出通道及加载安排

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留；两版都未建立完整路径收益。
属于新运行 `20261007-100531-gemm9070xt` 的第 1 次假设（上限 10），没有重复计入 EXP-002。

## 新基线及算子排名

主仓基线 `747fc5785dc976edb233ac339ef2f46bf3691494`，宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。
重新核对现有 production 构建的 65 个源身份、76 个产物身份；Vulkan 实测 RX 9070 XT / AMD proprietary / 26.8.1 (LLPC)。
MSVC 14.51.36231、glslang 16.5.0。现有 private 模型及确定性照片；2258×1270、scale 1、单遍、无运动历史。
新计划为 **2368×1280、ViT 800 tokens、123 dispatches**。没有 SR/FG 或游戏。
完整宿主低插桩四批（各预热 100、测量 200 帧）均值依次为 10.738119 / 10.752033 / 10.784565 / 10.871545 ms。
独立分段诊断完整区间 10.808857 ms，其中算子 10.388995 ms；该插桩口径不与普通计时直接相减。
普通 standalone 逐层排名如下，诊断时间只用于选候选，不替代宿主验收：

| 算子 | M×N×K | 次数 | 工作组/次 | waves/组；每 wave 输出 | VGPR | 诊断合计 ms |
| --- | --- | ---: | ---: | --- | ---: | ---: |
| CCVit1DFfnContract/gemmprojws | 800×1024×4096 | 8 | 104 | 8；32×32 | 70 | 0.375 |
| CCVit1DFfnExpand/gemmvact | 800×4096×1024 | 8 | 208 | 4；64×64 | 167 | 0.346 |
| CCVit1DQKV/gemmvqkvnorm | 800×3072×1024 | 8 | 156 | 4；64×64 | 167 | 0.271 |
| CCVit1DProjection/gemmproj | 800×1024×1024 | 7 | 200 | 4；32×32 | 75 | 0.218 |
| CCSplitSwin16HFfwdProj/gemmproj | 3040×512×512 | 16 | 380 | 4；32×32 | 75 | 0.192 |
| CCSplitSwin16HProj/gemmproj | 3040×512×512 | 15 | 380 | 4；32×32 | 75 | 0.182 |
| CCVit1DProjection/gemmprojt | 800×1024×1024 | 1 | 200 | 4；32×32 | 75 | 0.032 |
| CCSplitSwin16HProjPool/gemmpool | 3040×512×512 | 1 | 192 | 8；32×32 | 79 | 0.014 |
| CCSplitSwin16HFinalHead/gemmnores | 800×1024×512 | 1 | 104 | 8；32×32 | 78 | 0.009 |
| CCDecInputUpsample/gemmvqkvs | 800×512×1024 | 1 | 100 | 4；32×32 | 98 | 0.007 |

C512 `ffwd3w` 另占 0.449 ms / 16 次，每次 192 工作组、4 waves/组，每 wave 处理一个组的 32 tokens，VGPR 182。
每个 FFWD 调用含八个独立组，矩阵依次为 M=3040、N/K=64/512、256/64、64/256；中间激活和量化不能省略。
全网其它大头仍为 C32 普通层、pre/post、persistent DS/UPS；它们融合多种运算，不写成一个虚构 GEMM 形状。
M=3040 来自 C512 实际 tile-grid；计划中的 padded-window tokens=3200 不能直接当 GEMM M。
FinalHead 在 pooled 尺寸上 M=800，亦不沿用 C512 的 M。

## 假设和两版实现

`gemmvact` 的 800×4096×1024 每帧调用八次，原 208 工作组、四 waves/组、每 wave 64×64。
每线程 128 个 FP32 累加值、167 VGPR。改成八 waves/组、每 wave 64×32，保持工作组 64×256，
原 N-pair 权重和所有输入/输出格式、舍入、逐输出 K 累加顺序不变。
这与 EXP-002 同时拆 M/N 以及只拆 M 的旧候选不同；此处保留每 wave 的 64-token 权重复用。

- 版本 1：`WM=1 WN=8 MFRAG=4 NFRAG=2 KFRAG=2`，VGPR **167→95**，无 LDS/scratch。
- 版本 2：同分工，`KFRAG=8`，VGPR **92**，无 LDS/scratch。原 KFRAG=2/PIPE=3 只 prime 两步，
  不能在 chunk 内填充未来数据；扩大 chunk 允许已有预取逻辑工作，仍按 K=0,16,…累加。
- 原型仅替换离线 shader 副本的 `gemmvact`，生产、host 常量及 Linux 未修改。
  若有效，本计划要求新增按形状选择的入口、同步 marker/host 常量和打包注册后扩大验收；本次未达到该阶段。

## 性能与正确性

相同 production DLL、D3D12/Vulkan 完整 NR 区间、depth=3，逐次独立进程。
每批 100 帧预热、200 帧测量；每版 ABBA×3，同路径控制 ABBA×2。统计/ISA 构建仅作诊断。

| 比较 | 基线 ms | 候选 ms | 节省 ms | 各 ABBA 组节省 ms |
| --- | ---: | ---: | ---: | --- |
| N-only | 10.842582 | 10.895785 | -0.053204 | -0.080102 / -0.060132 / -0.019377 |
| N-only + KFRAG=8 | 10.898221 | 10.914178 | -0.015957 | -0.014361 / -0.027799 / -0.005711 |
| 同路径控制 | 10.898409 | 10.899766 | -0.001356 | 0.002295 / -0.005008 |

普通 standalone 扩张段由约 0.346 ms 变为 0.372 / 0.378 ms；减少寄存器没有转化为速度提升。
不能仅凭 VGPR 下降推断占用率/吞吐提高；拆分增加了激活读取的重复度，尚未用硬件计数器证明精确瓶颈。
32 批完整宿主输出逐字节一致，共 6400 个测量帧；六份 standalone 输出也与独立基线一致。
宿主每批帧耗时标准差范围 0.064599–0.154377 ms，保留全部批次。
未采样 GPU 时钟、温度、持续后台负载；已知游戏/benchmark 进程检查未发现占用，WMI 进程查询不可用。
没有对失败候选扩大历史、边界、中间值、host ABI 或游戏验收；不将静态输出一致等同于这些未测项通过。

## 身份与复现

基线重编 `gemmvact` 与独立保存 SPIR-V 完全相同。

- rebuilt: `c48c1619dde7995664f7b0e6e34c4bd51abf0d6b20cac70b65daf186b01efbdb`。
- n-split: `1b391bad806bf3acab69ce69bf7f8809077928f563b539a797561a9041fc09c0`。
- n-split-k8: `1d12d283723fa7de91f7dc8ac42e376afc03f470d071ec82aa4ebe42fa759bc5`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <assets> --in-image <photo> --out-image <rgba> --pipeline-cache <cache> --warmup 100 --repeats 200 --per-layer --dispatch-grid
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <packed-output> <photo>
```

工具及计时解释见 [host profiling](../../docs/windows-host-profiling.md)。原型/原始 CSV、ISA、身份清单留忽略目录。
本次只提交本记录、实验索引和状态；没有生产修改、宿主提交、发布构建、安装、部署或推送。
下一步研究 C512 FFWD 的双 token tile 驻留，避免重复本次已否决的扩张拆分。
