# EXP-163：C512 attention 首个 bias pair 提前加载

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261011-001700-ten9070xt` 第 8/10 个假设；1 版实现，0 次追加确认。
起始主仓 `7f741298f48b2b87ac11f1b731876dca5ac33445`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

四个key fragment仅预读概率c0/c1的原只读FP32 bias，在原指数位置消费；其余bias pair仍原位读取。未改变QK累加、精确FP32分母、half指数或context。不同于EXP-117的整面板宽读，本项测试加载时间；不同于EXP-146的C256 DS。

disabled SPIR-V精确，候选整网FP32一致。driver.text26880→27136 B、指令3882→3913，VGPR184→190、SGPR59、LDS9216 B/scratch0。复核发现基线已经在QK前预读全部bias：原ISA文本行3239..3246共8条b128，QK第一WMMA3254，首个wait_load3266。候选3258..3276读bias，QK首WMMA3284，首个wait_load3296；依然同一结构位置，没有实现新的跨QK提前交付。候选打散读合并，bias读8条变13条（5b32+4b128+1b64+3b96）。这些是指令位置，不是周期。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.930771 / 10.922131 / 10.921556 / 10.919724 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

基线已具有所需QK前预读，候选没有实现额外隐藏机会，且增加读取数和寄存器；按mechanism_not_realized提前关闭，没有性能结论。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
仅disabled与静态FP32精确；未运行本项packed/性能A/B及反序确认，未扩大格式、边界、历史、中间值、replay、fallback或无缓存验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / attn SPIR-V：`4bafe70c5fa393acc1b0d6c1f53715a48f1cc6b9adf137d42d23caabdf6c62dc`。
- early / attn SPIR-V：`41d13184b527c9c70e39de83ff32fa85ae36e3690ba4555d8d018665a4478d7f`。
- prototype.patch：`313d3a8e2d44da21d5e89c26513386646d477e0e59249a8e9b8b5338d9325030`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：decoder宽路径仍有8条连续u16增益读取，尝试用对齐16字节原位读取合并；完成十次保存核验后关机。
