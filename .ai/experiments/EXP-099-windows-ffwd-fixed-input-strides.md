# EXP-099：C512 FFWD 固定输入步长与滚动循环

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-193306-lossless9070xt` 第 3/10 个假设；1 版实现，1 次追加确认。
起始主仓 `7d789181cea395ad707388786c470434c143d224`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

16 次 C512 FFWD 派发约占 0.437 ms；图类型 CCSplitSwin16HFfwd 固定 C=512，PushFFWD 同样赋值 512。原型把输入/输出地址中的 pc.C 固定为 512，保留 M、所有 MMA、激活、转换和权重。ffwd3 / ffwd3w 都构建；目标只派发 ffwd3w。与 EXP-066 的 ViT 维度特化和 EXP-017 的 token 分工不同。第一版构建被源码展开器误展开，修复循环为 k0 != 512u 并保留 dont_unroll，实际访问顺序仍为 0/128/256/384；一次构建修复，不新增变体。

关闭开关的两份 SPIR-V 与独立基线完全一致。有效 fixed-rolled 的目标 FP32 和全部 packed 回读精确一致。ffwd3w 驱动代码 .text 31232→30464 B，静态指令 4185→4079；VGPR 183、SGPR 22，LDS/scratch 均为零。ffwd3 未在目标执行，未声明其 ISA 结论。失败构建后曾误运行复制基线的三帧诊断，已显式标记 candidate_valid=false，排除候选验证；该次在找不到 build.json 时停止，未进入性能计时。筛选脚本已补充构建清单和 shader 哈希前置校验。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.999239 / 11.010161 / 11.007372 / 11.000571 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-fixed-rolled | 11.107626 | 11.096617 | 0.011009 | 0.034562 / 0.005014 / -0.006549 |
| ba | 11.120502 | 11.130691 | -0.010189 | -0.015214 / -0.003098 / -0.012255 |
| confirm-control | 11.132268 | 11.126041 | 0.006226 | 0.006572 / 0.005880 |
| control-fixed-rolled | 11.110116 | 11.123499 | -0.013383 | -0.006337 / -0.020429 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测完整路径 11.107626→11.096617 ms，省 0.011009 ms，但三组方向混合；同路径控制均差 -0.013383 ms。唯一反序确认 11.120502→11.130691 ms，慢 0.010189 ms，三组均慢；确认控制均差 +0.006226 ms。前后方向相反，未确认完整路径收益，unproven，不保留。

ADL 合计 591 个有效样本，核心时钟 77–2825 MHz，edge 41–59°C，hotspot 42–83°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / ffwd3 SPIR-V：`498de530c88d09f3a266d4dd2c638749cddc3a3a58cec9827881bc22fad217e4`。
- disabled / ffwd3w SPIR-V：`3ad3da9ee684c7951e5d37a7bca62af1ca9e8357b9abbeabbd2cd662aeaaabdb`。
- fixed-rolled / ffwd3 SPIR-V：`1d44563ed23363fe418c4a6b46b2f884bdcf403e8ef9c903470bb6ce106be35b`。
- fixed-rolled / ffwd3w SPIR-V：`113faedbee4bbebe0a4526a2508008e8afd149087fe309723f4abea02cc125fd`。
- prototype.patch：`588c168d2c75d7b5e81871f04f1ca29d2244d1f21eb6cf3d91b8681f2899f182`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续剩余七次额度；检查 wide UPS 输入投影在未偏移且未越界窗口中能否精确省去坐标夹取与拆分。
