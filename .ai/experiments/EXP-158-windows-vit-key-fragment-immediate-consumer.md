# EXP-158：ViT K 片段逐维读取与双 query 即时消费

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（correctness_failed）。
运行 `20261011-001700-ten9070xt` 第 3/10 个假设；1 版实现，0 次追加确认。
起始主仓 `a67a075221870ff7f390f8c469b3f395b8c34dce`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

仅在完整 K16 tile/QB=2 路径读原 K0，立即更新原两个 query 的 logit，再读 K1；概率和分母/context 仍按原 b 顺序执行。与 EXP-023 的全 K/V cache 和 EXP-116 的加载完成后 logit batching 不同，本次意图限制只存活一个 K 片段并提早开始消费。尾块不改。

disabled SPIR-V 精确，候选构建和GPU执行成功；但独立整网 FP32 逐字节比较失败，输出SHA256基线17fff022fac3014f6aca032b95105d0f378adc671003a52e440a332e0c89b7fe、候选d2357b297e41f90971a667d8a075f8ee07e6f647d0c2d2107e10fec0862f9979。源级每个logit的d顺序保持，不足以证明驱动下游重排或片段传递等价；未进一步归因具体差异。driver.text14976→14848 B、静态行2137→2128，VGPR132/SGPR33、LDS/scratch0保持，这些资源或代码缩减不能覆盖正确性失败。差异计数保存在独立证据目录。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.930771 / 10.922131 / 10.921556 / 10.919724 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

按correctness_failed关闭；没有性能A/B，不接受任何容差，也没有保留或提速结论。原型仅在忽略目录，生产两仓未改。

保存 1 份整网 standalone 数值结果，其中 0 份一致。
FP32前置检查失败，未运行packed/完整性能、其它格式、边界尾部、运动历史、中间值、replay、fallback、两套配方或无缓存验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- streamed / vitattn SPIR-V：`56462312a498d60daa8b37541bd9b58f67c3592c23f63aa9cef074b2f3cfd755`。
- prototype.patch：`8a432dd6ff6b753c58d2248cac91374760845562255b4641bb89818d55b2dd9d`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查ViT收缩epilogue每四个结果即时量化，尝试缩短half结果生存期而保持单元素表达式；十次完成保存后关机。
