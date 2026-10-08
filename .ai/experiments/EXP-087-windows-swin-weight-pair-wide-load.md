# EXP-087：C256 成对权重的显式 16 字节读取

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-162203-lossless9070xt` 第 1/10 个假设；1 版实现，0 次追加确认。
起始主仓 `8ea376b56619081a6c8abb140470bf835013e3ea`；宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。

## 假设与实现

C256 DS/UPS 的 NR_WPAIR_FILL2_U 原来按两个 uvec2 读取相邻片段，候选用同 binding 的 uvec4 视图读取完整 16-byte lane record，再将 xy/zw 交给原 NR_FRAG_Q。q 为 base/4 + row-pair*kfrags*128 + k*128 + lane*4；原 weight packing 的 base 具有 16-byte 对齐，故地址与两次原读取相同。权重、FP8 位模式、矩阵运算、累加次序、布局和同步均未改。与 EXP-030 的提前加载、EXP-068 的 coherent 激活读取不同。

两份 disabled SPIR-V 完全复现基线；目标 FP32 与全部 packed 宿主输出逐字节一致。实际机器码变差：DS .text 224384→279168 B、静态指令 31956→38161，UPS 213120→269056 B、28897→35431；两者分别新增 3040 条 v_perm_b32。DS 使用 225 VGPR、UPS 198 VGPR，均为 32768 B LDS、零 scratch。没有把寄存器下降或三帧诊断时间当作收益。ADL 0.5 秒采样包含启动和预热，不是逐帧同步。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.019102 / 11.015484 / 11.018076 / 11.025782 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-wide | 11.048334 | 11.328711 | -0.280377 | -0.278475 / -0.285959 / -0.276696 |
| control-wide | 11.061986 | 11.063740 | -0.001754 | 0.002411 / -0.005920 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

完整宿主三组全部退步，平均由 11.048334 增至 11.328711 ms，增加 0.280377 ms；同路径控制均差为 -0.001754 ms、两组范围 -0.005920 至 +0.002411 ms。退步明确，关闭为 regressed，不追加确认。原型仅存在忽略目录，生产路径未改。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinpds256 SPIR-V：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup256 SPIR-V：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- wide / fswinpds256 SPIR-V：`f7b30aa0734213af47508eb7de934b6b4f7053e087185674d3c61dc582947ebb`。
- wide / fswinpup256 SPIR-V：`a4106f081768ea99f9a3e369068c1b928e978c115e077f0fab7e30badc32dce0`。
- prototype.patch：`7b2191d85c9f293c66a302d3f8be5144d4db7a4eafc9d9b4a2bb8d73fc48dc17`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：依据源码进一步定位不同机制；C32 逐 query softmax 已由 EXP-003 测过，不重复消耗额度。优先检查图像 post 的独立工作安排及边缘权重/残差数据流。
