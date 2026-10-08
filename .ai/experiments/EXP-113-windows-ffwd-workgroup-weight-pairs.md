# EXP-113：C512 FFWD 相同权重组的工作组成对排列

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261009-005048-lossless9070xt` 第 7/10 个假设；1 版实现，0 次追加确认。
起始主仓 `7d98351cc59508b79b43f6a5aa86e394fa6a7718`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

原 group-major FFWD 工作组轮转八个权重组。候选仅在每个完整的 16-workgroup 块内按 0/8/1/9/…/7/15 重排，最后不足 16 个的尾部保持原编号，期望相邻工作组复用同组权重。各 wave 原来的 16/32 tokens、MMA、K 次序、所有 half/FP8 舍入、写入地址和返回条件均不变，没有新增 LDS 或屏障。不同于 EXP-017 改变每 wave token 所有权、EXP-074 LDS 协作缓存。

CPU 检查 1..1024 个工作组的完整排列/尾部恒等式，以及 8296 个 M/WGW/FM 组合、2031648 个工作组-wave 所有权元组，均一一对应；包含所有 1..1025 的 M、生产阈值和大尾部，WGW=1/2/4/8、每wave=16/32 tokens。mapping proof SHA256 016797a6ee951b753f1d3de42f4fd3970a774c0dc1b14ef267747616b9ce8e04。两个 disabled 模块精确复现基线；目标 FP32 和所有 packed 全图逐字节一致。目标 ffwd3w .text31232→31360 B、静态指令4185→4219、VGPR182→167、SGPR23→24，零 LDS/scratch；映射增加标量计算，资源减少不能直接视为加速。FM1 已构建但目标没有执行，尚无该路 GPU 数值覆盖。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.992494 / 10.960786 / 10.984746 / 10.959362 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-paired | 10.990525 | 11.015209 | -0.024684 | -0.005835 / -0.030051 / -0.038165 |
| control-paired | 10.991294 | 10.998378 | -0.007085 | -0.001385 / -0.012784 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

regressed，不保留。初测完整路径 10.990525→11.015209 ms，慢 0.024684 ms，三组均慢：0.005835/0.030051/0.038165 ms；同路径控制组差为 -0.001385/-0.012784 ms。后两组回退明显大于同期控制组差，筛选没有出现收益，直接淘汰且不扩大昂贵验证或追加确认。这是本次目标配置的筛选结论，不外推所有尺寸或机器。

ADL 合计 314 个有效样本，核心时钟 41–2732 MHz，edge 38–54°C，hotspot 38–79°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / ffwd3 SPIR-V：`498de530c88d09f3a266d4dd2c638749cddc3a3a58cec9827881bc22fad217e4`。
- disabled / ffwd3w SPIR-V：`3ad3da9ee684c7951e5d37a7bca62af1ca9e8357b9abbeabbd2cd662aeaaabdb`。
- paired / ffwd3 SPIR-V：`f460a5ad72f744098daa848e0812f5508251d06a7fd9059dbe93d261ef1e7443`。
- paired / ffwd3w SPIR-V：`a9b0ed15e4d96d0c9198d5f7b3dda3c8490d7ce500f4baed46a2648c2081b86d`。
- prototype.patch：`b04a53790f6f48aefc9f5c0ac7a7140205ca08c3c52a0c0992e9cf7e31b9b222`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：审计网络内部资源依赖，尝试把纯缓冲区步骤之间的全局内存屏障限定到 activation arena，保留原阶段与访问掩码。
