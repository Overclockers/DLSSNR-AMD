# EXP-024：输入特征 LDS 成对存储

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-111149-memory9070xt` 第 2/10 个假设，1 版实现，1 次追加确认。
主仓基线 `7541b5662b2676ec39b52a074f47e0103d6c7a62`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

输入层占约 0.89 ms；原 ISA 每线程用 16 条 `ds_store_b16` 写入 16 个特征。原型将 `input_features` 从 1024 个 half 改为 512 个 uint，每两个相邻的原 `NR_F16(f[j])` 经 `packFloat2x16` 位打包写入。对应 cooperative load 的 offset/stride 以 uint 为单位减半。
每个特征的算术、独立 half 舍入、位模式和字节地址不变；load stride 仍为 32 字节，片段 offset 仍为 512 字节的倍数，保持 16-byte 对齐；原同步点不变。只改私有 `image_input.glsl` 与 `fswin_body.glsl`，不同于旧完整窗口特化或量化优化。

ISA 的输入特征写入由 16 条 `ds_store_b16` 变为 4 条 `ds_store_2addr_b32`；VGPR 124、LDS 6144 字节、scratch 0 均不变。普通逐层 pre 为 0.890→0.892 ms，standalone 全网 10.340→10.350 ms。减少静态写指令没有建立完整路径收益。

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
| ab | 10.863621 | 10.862628 | 0.000993 | -0.000424 / 0.011780 / -0.008377 |
| confirmation-control | 10.930738 | 10.935286 | -0.004548 | -0.012716 / 0.003620 |
| confirmation | 10.903906 | 10.896811 | 0.007094 | 0.022784 / -0.001480 / -0.000021 |
| control | 10.931158 | 10.928759 | 0.002399 | -0.002986 / 0.007784 |

初测平均节省 0.000993 ms，三组正负混合。反序确认平均节省 0.007094 ms，但三组分别约 +0.022784 / -0.001480 / -0.000021 ms，没有一致的候选优势。单独第一组不能作为验收结果，判为未证实，不保留。

## 正确性、身份与边界

40 批 / 8000 个测量帧，批内帧标准差 0.058818–0.077193 ms。
40 份 packed 全图和 3 份 standalone FP32 全图逐字节匹配独立基线；基线重编 SPIR-V 字节相同。
生产源码始终未应用原型。性能未通过，未扩大中间值、其它尺寸/格式、历史/重放、回退和 main/host 集成验收。
静态照片一致不代表上述未测项通过。WMI 查询被当前沙箱拒绝；时钟/温度及持续后台负载未采样。

- packed / fswinimagepreds32 SPIR-V SHA-256: `9f5cfc61d24dd9d2b7789c52c7a3d566552828423c7378b229b8c3d3387959d9`。
- rebuilt / fswinimagepreds32 SPIR-V SHA-256: `6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- prototype.patch SHA-256: `d650b031540828552550ff030e5f1da6c7c3533d4dfcccaafaf4d3b12519fa22`。

```text
profile_host.exe <production-dll> <baseline-or-candidate-assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 100 --repeats 200 --per-layer
```

源快照、编译定义/工具/产物哈希、完整 CSV、图像、ISA 和原型留在忽略目录，位置在本地 ledger。
只有本记录、索引和状态进入本地主仓提交；宿主没有改动或提交，没有 Linux 修改、推送、游戏、部署、安装、打包或发布验证。
下一步：另立假设测试输入特征 LDS 转置；当前成对写入仍保留 32 字节的 token 间距，静态地址分布提示写入集中在少数 bank，尚无硬件冲突计数证据。
