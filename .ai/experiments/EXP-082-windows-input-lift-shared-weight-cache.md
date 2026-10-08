# EXP-082：输入投影权重的工作组共享缓存

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-130553-swin9070xt` 第 6/10 个假设；1 版实现，0 次追加确认。
起始主仓 `14f72701d0f542586582924202b9df6a9b8cf352`；宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。

## 假设与实现

输入 pre 的两个 wave 对相同 1024-byte FP16 lift 矩阵重复读取。候选在 nr_prepare_features 前半段由 64 个线程各搬运四个 uint 到 256-word LDS，借用原有 feature barrier 发布；后续 A16 cooperative load 以 8-word stride 和 0/128-word offset 读取同一 half 数据。原矩阵操作、输入特征、half 转换、packing、控制和采样不变；disabled 使用原 global load。与 EXP-071 的每-token 显式复用不同，这里跨 wave 共享；与 EXP-074 不同，不增加 barrier。对照包含 EXP-076。

nr_graph 的 plain_lift 大小固定 1024 bytes、put(...,16)/2 提供偶数 half 地址；256 个缓存 word 恰好覆盖原矩阵。独立 GPU probe 将原 global A16 与 packed LDS A16 对照，覆盖全部 65,536 种 half 编码，131,072 个观测值、262,144 字节完全一致；byte stride 32、fragment offsets 0/512 均满足对齐。probe 源 SHA-256 e607abd7171b2859a59045880d48c01732b571edcc6796d9e133d3b2b19abc3b，输出 SHA-256 7a268154ab8093d08c4c903e6ab6105d26b8b6729944bb9d91fa7d3d023d15c4。普通 pre 与实际 runtime_preencode 的 disabled SPIR-V 复现基线，FP32/packed 全图精确一致。VGPR 124→128，LDS 6144→7168 B，零 scratch；runtime .text 26240 B 不变，静态指令 3577→3600。ADL 0.5 秒只读采样含启动/预热，不是正式帧同步。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.840392 / 10.891116 / 10.921970 / 10.937182 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-cache | 11.048275 | 11.061101 | -0.012826 | -0.015475 / -0.012950 / -0.010053 |
| control-cache | 11.065447 | 11.061079 | 0.004368 | 0.003767 / 0.004969 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

完整 NR 三组均变慢，平均增加 0.012826 ms，分别增加 0.015475/0.012950/0.010053 ms；同路径控制最大绝对组差 0.004969 ms。关闭为 regressed，不保留、不追加确认。即使复用已有屏障，额外 LDS 与读取/存储组织仍未形成净收益。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- cache / fswinimagepreds32 SPIR-V：`2323d8c68b17d752d301d3a730c6df65ad448574e7f0bd284663ed6632cac74b`。
- cache / runtime_preencode SPIR-V：`ba15ff82d2517c08bfb56fb3834f6c5ec36ce22d8a794a1076a8a0ed740831f8`。
- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- prototype.patch：`f0d3bdca6c6d1215ab8a2ee889323fcd8801e1f67a224930cece4e2dcfb8b49e`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：审计 image post 的内部窗口坐标，对已证明完整在界内的窗口消除重复整数 clamp/min，边缘和不规则输入保留原公式。
