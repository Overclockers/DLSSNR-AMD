# EXP-157：FFWD Q0 按权重分组执行独立 MMA

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261011-001700-ten9070xt` 第 2/10 个假设；1 版实现，1 次追加确认。
起始主仓 `5b989885aa4a3e28c3d8f1e364bcbc27ce7f12fb`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

仅改变 C512 FFWD Q0 的两个 token query 与两个输出片段的独立 MMA 排列。每个累加器保持 K0..3 顺序、FP32 WMMA、原 half 激活及 FP8 量化；已保留的 peeled Q2、权重与同步不变。不同于 EXP-105 的 ViT expansion，本项针对 FFWD Q0 相同 A 操作数连续消费。

关闭宏 SPIR-V 精确；目标 FP32 11,470,640 个值及全部 packed 回读一致。.text31616 B，指令4161→4163，VGPR189/SGPR23、LDS和scratch0均保持。完整 weight-load 指令序列相同。排除前64条首投影WMMA后，基线第64/65条分别使用A v[0:1]/v[2:3]；候选两条均用v[0:1]配原两个B query fragments，实际分组实现。总320条WMMA不变；机制报告与两份原始ISA保存在独立证据目录。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.930771 / 10.922131 / 10.921556 / 10.919724 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-grouped | 10.916600 | 10.920827 | -0.004226 | -0.001487 / -0.012603 / 0.001413 |
| ba | 10.954505 | 10.952636 | 0.001869 | 0.001022 / 0.003118 / 0.001466 |
| confirm-control | 10.957101 | 10.952892 | 0.004210 | 0.003372 / 0.005047 |
| control-grouped | 10.933900 | 10.929239 | 0.004661 | 0.005657 / 0.003665 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测候选增加0.004226 ms，各组方向不一致；同路径控制节省0.004661 ms。唯一反序确认节省0.001869 ms，低于确认控制0.004210 ms。方向未跨次复现，收益未能区别于控制波动，按unproven关闭，不保留。

ADL 合计 590 个有效样本，核心时钟 713–2877 MHz，edge 34–55°C，hotspot 34–80°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / ffwd3w SPIR-V：`2d3400f917303b26afc3290d6c52aada81fca9c44f6f0665ce60598316f94b5a`。
- grouped / ffwd3w SPIR-V：`25bd401e2889c7372b9d22c1ccf2cdd327a7a359bda58142b1c2e001416fdfeb`。
- prototype.patch：`3e7207e108b8171207207c1b7a3000dd380ca9f8df39640c0895aace0f7added`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：调查 ViT key fragment 的逐维读取与即时双 query 消费，针对现有完整K/V预取和logit batching未缩短的K片段生存区间；十次完成保存核验后关机。
