# EXP-073：C512 前馈 middle 成对流式消费

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-115341-shapes9070xt` 第 8/10 个假设；1 版实现，0 次追加确认。
起始主仓 `2b52db0a331a4648a1b68903f64956d803523b03`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

ffwd3w 原来同时保存两个 token tile 的四行 middle FP32 结果，再量化供 final 使用。候选在原 64 行 chunk 内按两行计算、完成原 half 激活/FP8 量化后立即更新已有 final 累加器。middle 每个输出的 K=0..3、final 的 K=0..15 顺序完全保留，权重、packing、舍入、尾部和输出地址不变。区别于 EXP-031：这里 final 累加器本来就在基线中存活，不因流式消费而新增；没有 LDS 或 barrier 变化。

禁用 SPIR-V 精确复现基线；FP32 整网和所有 packed 宿主输出逐字节一致。middle 同时存活的 fragment 由 8 减为 4，驱动 VGPR 182→127，SGPR 23、LDS/scratch 0。.text 31232→31488 B，指令 4185→4266，wait/delay 指令增加。ADL 每 0.5 秒只读采样，包含初始化/预热，并非帧同步。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.862125 / 10.870288 / 10.903622 / 10.937338 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-stream | 11.031274 | 11.081718 | -0.050444 | -0.057342 / -0.059159 / -0.034831 |
| control-stream | 11.107162 | 11.113090 | -0.005928 | -0.000794 / -0.011062 |

完整 NR 平均慢 0.050444 ms，三组分别慢 0.057342/0.059159/0.034831 ms；对应控制最大绝对组差 0.011063 ms。寄存器明显减少仍不能转化为完整路径收益，提前关闭为 regressed，不追加确认。

保存 22 份宿主回读、4006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / ffwd3w SPIR-V：`3ad3da9ee684c7951e5d37a7bca62af1ca9e8357b9abbeabbd2cd662aeaaabdb`。
- stream / ffwd3w SPIR-V：`9491b2690dbb306b55328853ef7150203bc5aa8157e7e7b64e9068cb5547f905`。
- prototype.patch：`d7ace1ec56eaa2fd3bc9c78069f2fd3784b1949e2a47d96e45b5a2ed120fe2df`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查同一 group-major 工作组的四个 wave 读取相同 Q0 权重，测试一次协作加载到 LDS 并保留尾部通用路径。
