# EXP-151：ViT context的V片段延后读取

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261010-224000-ten9070xt` 第 6/10 个假设；1 版实现，0 次追加确认。
起始主仓 `ea438b8f497e9e81ccf0d506f6d11561c16f8097`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

完整K块先保留两组原概率B片段，再逐输出维度读取V，立即消费两个query的context。每组QK、half指数、分母、概率量化/key排列、K顺序和tail保持原样，V总读取次数不增。区别于EXP-023的提前K/V预取、EXP-116 logit分批与EXP-128 C512 context交错。

禁用SPIR-V精确一致，目标FP32全图和23份packed回读精确一致。绑定driver证据确认首个global_load_tr_b64之前的QK WMMA从0增加到4；该V读取opcode总数16不变。VGPR132→119、SGPR33不变、零LDS/scratch；.text14976B不变，指令2137→2155。门槛通过后执行完整ABBA和同路径控制；该局部机制不用于周期节省预测。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.963855 / 10.966668 / 10.968056 / 10.955780 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-late | 10.985825 | 10.996851 | -0.011026 | -0.010808 / -0.016731 / -0.005540 |
| control-late | 10.997021 | 10.996623 | 0.000397 | 0.001270 / -0.000475 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

regressed，不保留。完整NR 10.985825→10.996851 ms，平均慢0.011026 ms；三组均慢0.010808/0.016731/0.005540 ms。紧随控制组差+0.001270/−0.000475 ms，候选退化大于控制差且方向一致；初筛拒绝，不追加反序确认。降低存活期同时推迟读取，最终未获得净收益。

ADL 合计 300 个有效样本，核心时钟 0–2732 MHz，edge 39–55°C，hotspot 39–81°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
已做目标FP32、packed和完整初测/控制。因初测退化，未做反序确认、nobda/其它modes、边界/历史/中间值/replay/fallback和两套配方完整验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- late / vitattn SPIR-V：`f90ca28554ff8b50f2e51a0ee5818a614d37d3474dc9f58b16bd51526f13303d`。
- prototype.patch：`324d367a2aba3a1ae8d14654c5002de5b814d7105bab7d73d64e36c27b812cb8`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续第7/10项：C64 FP32残差gain采用对齐vec2读取并保留奇地址回退。
