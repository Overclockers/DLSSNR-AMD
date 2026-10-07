# EXP-017：C512 FFWD 独立 token 的 wave 分工

日期：2026-10-07。结论：未保留。新运行 `20261007-100531-gemm9070xt` 第 2 个假设，仅一版实现、一次反序确认。

## 依据与实现

新基线/形状排名见 [EXP-016](EXP-016-windows-vit-expand-n-split.md)。C512 FFWD 每帧 16 次、M=3040，
各调用含八组的 512→64→256→64 矩阵；当前四 waves/组，每 wave 32 tokens，182 VGPR。
候选改成八 waves/组，每 wave 16 tokens；每工作组仍覆盖一个组的 128 tokens，192 个工作组的映射不变。
`NR_FFWD_FM=1 NR_FFWD_WGW=8` 使用原有单 tile 算术实现，无跨 wave 归约；保留每输出 K 顺序、
全部 FP16/FP8 舍入、激活、权重及 N-pair packing。不是 EXP-003 的 LDS 限制/加载视图或 EXP-006 的融合。
编译后 VGPR **182→110**，LDS/scratch 仍为零。普通 standalone 该段 0.454→0.452 ms，基本持平。

## 身份、环境和测量

主仓 `628d190311b4992d483d9b638e9b0438821a6ea7`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。
production DLL 及其它 shaders 均沿用本轮已验证身份；只修改忽略目录中的一个 `ffwd3w` 模块。
基线重编逐字节相同；glslang 16.5.0，MSVC 14.51.36231。RX 9070 XT / 26.8.1 (LLPC)。
2258×1270 输入、2368×1280 工作尺寸，R11G11B10、scale 1、单遍，确定性照片、无历史/SR/FG。
完整 D3D12 输入复制至输出复制区间，depth=3；每批预热 100、测量 200 帧。
初测 ABBA×3、同路径 ABBA×2；唯一确认反序 BAAB×3。每个数字是批均值，保留全部批次。

| 比较 | 基线 ms | 候选 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab | 10.883915 | 10.904115 | -0.020200 | -0.033799 / 0.000064 / -0.026865 |
| control | 10.931083 | 10.917653 | 0.013430 | 0.020613 / 0.006246 |
| confirmation | 10.905145 | 10.917992 | -0.012847 | -0.011227 / -0.017524 / -0.009790 |

初测期间有约三秒的本地 CPU 诊断工具编译，与测量部分重叠；不据初测小差异认定收益。
反序确认期间没有并行构建/其它 GPU 实验，三组仍稍慢，因此结束该候选，不挑选有利子集。
32 批、6400 个测量帧；各批帧标准差 0.073125–0.173736 ms。
GPU 时钟、温度及持续后台利用率未采样；没有游戏测试。

## 正确性与边界

32 份 packed 宿主输出和三份 standalone FP32 输出均与独立基线逐字节一致。
寄存器/ISA 诊断不用于验收时间。候选没有性能依据，因此未扩大边界尺寸、历史、单算子中间值或 host ABI 验收。
生产与 Linux 未改；将来若复测有效，仍须按验证形状注册独立入口，同步常量/marker/权重打包，不能直接全尺寸替换。

- rebuilt SPIR-V SHA-256: `3ad3da9ee684c7951e5d37a7bca62af1ca9e8357b9abbeabbd2cd662aeaaabdb`。
- single-tile SPIR-V SHA-256: `0fb378c25440386a70855ea4c759c35343c3d66b74e20eea3a5f31d2723e43a8`。

```text
profile_host.exe <production-dll> <baseline-or-candidate-assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <assets> --in-image <photo> --out-image <rgba> --warmup 100 --repeats 200 --per-layer
```

只提交公开实验记录/索引/状态；原型、ISA、CSV、输出和逐文件身份留忽略目录。宿主无提交、没有部署/打包/推送。
下一步：单独研究 ViT 800×1024×1024 投影，仅 200 工作组，避免影响共用内核的 C512 形状。
