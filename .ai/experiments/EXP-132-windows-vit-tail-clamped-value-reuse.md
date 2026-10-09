# EXP-132：ViT 尾块 clamped V 片段复用

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-235203-ten9070xt` 第 2/10 个假设；1 版实现，1 次追加确认。
起始主仓 `076637eb4cdfa80f6a508d1ffdd01fb4ba328177`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

800 tokens 的最后 K64 块有两个 padding K16 块，它们读取同一个最后有效 V tile。候选只在 guarded tail 内跨 kb 保留 vf[2]，仅当 j0+kb<=jlast 时重读；保持所有 K 加载/置零、logit、指数、half 分母归约、FP8 概率和 context MMA。每个尾块第一步必为有效tile，变量先赋值后使用。不同于 EXP-086 的外层循环拆分及 EXP-023 的整块预取，也不跳过 exp(0) 的分母贡献。

BDA与非BDA两模块关闭后均精确复现SPIR-V；8192种token长度的32768次tile地址比较一致，证明文件SHA256 `0dffab65ec56f395490117ba4164fa99504f665b48e31513e07af10a9b69caaf`。目标整网FP32与全部packed末帧精确一致。实际目标vitattn的driver .text14976→15104 B、静态指令2137→2168、VGPR132→136、SGPR33→34，无LDS/scratch；s_wait_loadcnt34→28，但增加4个标量条件分支。非BDA模块只编译，未在目标路线执行。未把静态指令变化当作完整路径收益。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.943105 / 10.917341 / 10.917457 / 10.944389 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-reuse | 10.906875 | 10.910436 | -0.003561 | -0.003058 / 0.001291 / -0.008918 |
| ba | 10.961710 | 10.961505 | 0.000205 | -0.006468 / 0.007909 / -0.000826 |
| confirm-control | 10.952741 | 10.953239 | -0.000498 | 0.001048 / -0.002045 |
| control-reuse | 10.923075 | 10.924226 | -0.001150 | -0.006349 / 0.004048 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测节省 -0.003561 ms，唯一反序确认节省 0.000205 ms；组间方向混合，未建立能区别于同路径控制波动的可重复收益，unproven，不保留。全部批次保留，没有追加第二次确认。

ADL 合计 612 个有效样本，核心时钟 0–2756 MHz，edge 34–57°C，hotspot 34–82°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- disabled / vitattnnobda SPIR-V：`3cdd38d4115abe4c9c063dd519cecd641ee9bc93956745163cf7f8e502d419ff`。
- reuse / vitattn SPIR-V：`03611e88600281824941c3607a69bdc5fc8f08d24d2daf285c12d76372a55c9a`。
- reuse / vitattnnobda SPIR-V：`4936ed9eac292a46a20fda97095b0682617df9543629dddeef4e1106456f051b`。
- prototype.patch：`eb3105abfff62c3196b5661d4c56a3b96c355c4e84c3f5d00ede72e7b30e418c`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查ViT QKV epilogue的Q/K/V分类分支能否移出每个分量循环，保持原half舍入与归约次序。
