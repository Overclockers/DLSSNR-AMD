# EXP-020：C256 下采样扩张阶段的结果块大小

日期：2026-10-07。结论：未保留；第 5 个假设，两版实现，token 分块唯一确认与初测方向相反，收益未证实。
主仓 `c0aac91c8bf88b9117d17f6c90183b228c2e89e1`，宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`；环境与新排名见 [EXP-016](EXP-016-windows-vit-expand-n-split.md)。

## 形状和机制

C256 persistent DS 覆盖八层、1596 个 window items/workgroups（190/209 个窗口交替），每组八 waves、每 wave 一个 head。
扩张按窗口/head 的完整 GEMM 为 **M=64、N=128、K=256**；默认每次保留 64×64 子块，
每线程 128 个 FP32 累加值。整个融合 dispatch 约 0.74 ms，229 VGPR、32768 字节 LDS、无 scratch。
目标是缩短独立结果的存活期；不改变任何输出沿 K 的计算顺序、激活/量化或 packed weights。

| 版本 | 独立结果分块 | VGPR | scratch 字节 | 普通分层该段 ms |
| --- | --- | ---: | ---: | ---: |
| 基线 | EXPAND_GROUP=4，全 64 tokens | 229 | 0 | 0.746 / 0.748（各候选相邻基线） |
| split-m | ESPLIT=1，每阶段半数 token fragments | 256 | 112 | 0.733 |
| split-n | ESPLIT=0，EXPAND_GROUP=2 | 206 | 0 | 0.770 |

两版均保持原工作组/队列及 32 KiB LDS；第一版反而触发更多寄存器及 spill，不能从源码数组变小推导机器资源减少。
第二版保留 64-token 权重复用，但增加激活 LDS 重读。它与第一版减少 token fragment 是不同分块取舍，未盲扫更多值。
这是对 C256 DS 未覆盖形状的测试；C128 DS 原已启用 ESPLIT、C256 UPS 原已使用两行分组，不重复计算它们的旧收益。

## 完整宿主 A/B

RX 9070 XT / 26.8.1 (LLPC)、MSVC 14.51.36231、glslang 16.5.0；2258×1270、工作 2368×1280。
R11G11B10 照片、scale 1、单遍、无历史/SR/FG，depth=3，production DLL，D3D12 输入复制至输出复制区间。
每批 100 帧预热、200 测量帧；两版各 ABBA×3，控制 ABBA×2，split-m 唯一确认 BAAB×3。

| 比较 | 基线 ms | 候选 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab | 10.875461 | 10.880999 | -0.005538 | -0.003045 / -0.008462 / -0.005105 |
| n-ab | 10.904245 | 10.923920 | -0.019675 | -0.024247 / -0.029666 / -0.005110 |
| control | 10.939738 | 10.943337 | -0.003599 | -0.006993 / -0.000204 |
| confirmation | 10.887398 | 10.867031 | 0.020367 | 0.020093 / 0.010193 / 0.030816 |

split-m 初测三组略慢，反序确认三组稍快，结果依赖测量会话/次序。不能只选择后一次的约 0.020367 ms 收益。
唯一确认用尽后判为 **unproven**；split-n 三组更慢。两版都不进入生产，不以 spill 本身代替性能判定。
44 批 / 8800 测量帧全部保留，批内帧标准差 0.061349–0.108097 ms。
GPU 时钟/温度及持续后台负载未采样；未进入游戏或发布阶段。

## 正确性、身份和边界

44 份完整宿主 packed 输出、六份 standalone FP32 输出与独立基线逐字节相同，baseline 重编 SPIR-V 相同。
保持原每个输出 K 次序的依据是既有 ESPLIT / EXPAND_GROUP 仅调整独立 m/h 循环，k 循环仍从 0 递增。
因性能未证实，没有扩大融合阶段中间数组、尺寸、历史/重放/格式或 main/host 正式集成验收。
若将来有新证据，需只给已验证形状注册独立入口，原 kernel 继续作为其它尺寸路径；本次没有新入口/marker。

- rebuilt SPIR-V SHA-256: `ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- split-m SPIR-V SHA-256: `a61e29930ce2ed72ea64d237ce73056697cf682e88989f05f30ce47e7792aa37`。
- split-n SPIR-V SHA-256: `471dcbd72694d8c66a776203b88635e59a96367a43eddaab920bd7fe92ad33e8`。

```text
profile_host.exe <production-dll> <baseline-or-candidate-assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <assets> --in-image <photo> --out-image <rgba> --warmup 100 --repeats 200 --per-layer
```

原型、ISA、CSV、图像和逐文件身份留忽略目录；生产/宿主/Linux 未改，只提交本记录、索引和状态，无推送。
下一候选为 ViT QKV 的单 head/wave 分工，保持其归一化树，避免继续扫描这两个已测 C256 分块。
