# EXP-150：C256下采样identity循环分离

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261010-224000-ten9070xt` 第 5/10 个假设；1 版实现，0 次追加确认。
起始主仓 `579f9544cdb1fb5f1231c35f5725066fe8a280a1`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

把uniform ds_identity条件放到最后的四个输出片段循环外，编译独立identity与fallback loop；两条路径的原权重、K顺序、half转换、零填充、边缘判定和输出地址公式保留。区别于EXP-027整个Swin代码复用、EXP-035下采样输入缓存，本项只改变最终shear地址控制流。

disabled SPIR-V精确复现基线；一处GLSL boolean literal编译修复后完成，仍1版候选。FP32全图与23份packed输出精确一致。driver确实生成单独identity/fallback输出projection区，identity区域_L3a65c至_L3b8bc无需原每片段shear除法控制序列；VGPR229→224，SGPR107、LDS32768B、零scratch不变。代码增长224512→245248B，指令32006→35033。gate绑定两个独立精确输出及当前ISA/ELF后启动完整测量。

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
| ab-split-repair | 10.990270 | 11.034662 | -0.044392 | -0.052858 / -0.048978 / -0.031339 |
| control-split-repair | 10.991159 | 11.000458 | -0.009299 | -0.014424 / -0.004173 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

regressed，不保留。完整路径10.990270→11.034662 ms，平均慢0.044392 ms。三组均慢0.052858/0.048978/0.031339 ms；同路径组差−0.014424/−0.004173 ms。即使重复分支消除和VGPR下降已经实现，整网仍退化；原始批次全部保留。初筛已足以拒绝，未消耗追加反序确认额度。

ADL 合计 319 个有效样本，核心时钟 732–2743 MHz，edge 38–55°C，hotspot 39–80°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
前置机制、FP32、packed和完整初测/控制已做；初测明确退化，未做反序确认，也未扩大到其它格式、边界shear尺寸、目标历史、中间值、replay、fallback和集成验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / fswinpds256 SPIR-V：`3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad`。
- split-repair / fswinpds256 SPIR-V：`a3faec3f457c59b9670c3a671576be04b5cb59b17df09aa186ee2e3a60282797`。
- prototype.patch：`34d59f12562a37679675faa10d93e9aa9fccfdeb18cfb5ef59ad3a3e799ad41c`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续第6/10项：ViT context延后V读取，只保留两组已量化概率并立即消费V片段。
