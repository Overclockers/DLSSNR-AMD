# EXP-023：ViT attention 完整 K/V 块预取

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-111149-memory9070xt` 第 1/10 个假设，1 版实现，1 次追加确认。
主仓基线 `93ec673921e92b2673b566732aa1a4a69cda2959`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

新逐层基线中八次 ViT attention 合计约 0.32 ms。原 ISA 按四个 K16 块逐次发出 K/V 加载，再等待对应数据。原型将完整 K64 块的 K/V 片段提前读入；尾块沿用原路径。每个 query 的 K16 次序、半精度分母归约、FP8 概率和 context 累加均保持。
这与旧 GEMM chunk 预取、ViT 输出块分割不同；只改私有副本的 `vit_attn_vt_chunk.glsl`。

ISA 确认完整块的八条 K b64 与八条 V 转置 b64 加载被移到计算之前；VGPR 132→131，LDS/scratch 均为零。普通逐层目标段 0.316→0.310 ms，完整 standalone 10.323→10.330 ms；该诊断不替代下表完整宿主验收。

## 环境与方法

Vulkan 新查询：RX 9070 XT / AMD proprietary driver / 26.8.1 (LLPC)。glslang 16.5.0、MSVC 14.51.36231。
新运行核对 65 个宿主源文件、76 个产物身份；独立保存 shaders，复用源码匹配的 production DLL 和现有离线工具。
输入 2258×1270 R11G11B10_FLOAT 线性照片；工作 2368×1280、800 ViT tokens、123 dispatches。
100% 模型尺度、单次 NR、depth=3；无运动历史、SR/FG 或游戏。
本轮新鲜完整宿主基线四批为 10.767961 / 10.773202 / 10.797066 / 10.797073 ms；基线已包含所有历史保留优化。
分段诊断完整区间 10.829574 ms，其中网络算子 10.408970 ms；不和低插桩测量混减。

验收计时为 D3D12 输入复制至输出复制的完整 NR 区间；每批预热 100 帧、测量 200 帧。
初测 ABBA×3、同路径 ABBA×2；如表中有 confirmation，则为唯一追加 BAAB×3、同路径 BAAB×2。
ISA/逐层诊断单独运行，不作为完整路径收益。所有批次均保留。

| 比较 | 基线 ms | 候选 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab | 10.853244 | 10.852833 | 0.000411 | -0.000252 / 0.002877 / -0.001392 |
| confirmation-control | 10.940365 | 10.931009 | 0.009356 | 0.002688 / 0.016023 |
| confirmation | 10.908561 | 10.900235 | 0.008326 | 0.028587 / -0.008026 / 0.004416 |
| control | 10.930120 | 10.914258 | 0.015862 | -0.005380 / 0.037105 |

初测平均节省 0.000411 ms，反序确认 0.008326 ms，但两批均有正负混合，确认的同路径控制也有 0.009356 ms 偏差。收益没有可靠超出控制波动，因此标为未证实，不保留，也不追加第三轮测量。

## 正确性、身份与边界

40 批 / 8000 个测量帧，批内帧标准差 0.070144–0.109031 ms。
40 份 packed 全图和 3 份 standalone FP32 全图逐字节匹配独立基线；基线重编 SPIR-V 字节相同。
生产源码始终未应用原型。性能未通过，未扩大中间值、其它尺寸/格式、历史/重放、回退和 main/host 集成验收。
静态照片一致不代表上述未测项通过。WMI 查询被当前沙箱拒绝；时钟/温度及持续后台负载未采样。

- prefetch / vitattn SPIR-V SHA-256: `07b136265376b053871e84f6f3994a1d9ec1c998b5001e05002862bbe7178ae2`。
- rebuilt / vitattn SPIR-V SHA-256: `1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- prototype.patch SHA-256: `29951572cbc40329af7ac2f818a4589576f310421b9680bebf29ce3c06525e34`。

```text
profile_host.exe <production-dll> <baseline-or-candidate-assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 100 --repeats 200 --per-layer
```

源快照、编译定义/工具/产物哈希、完整 CSV、图像、ISA 和原型留在忽略目录，位置在本地 ledger。
只有本记录、索引和状态进入本地主仓提交；宿主没有改动或提交，没有 Linux 修改、推送、游戏、部署、安装、打包或发布验证。
下一步：检查输入层 LDS 特征写入；其 ISA 仍有每线程 16 条 half store，可测试保留位模式的成对写入。
