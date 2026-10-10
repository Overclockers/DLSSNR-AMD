# EXP-141：FFWD 首段输入双 bank 预取

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261010-210227-efficient-five9070xt` 第 1/5 个假设；1 版实现，0 次追加确认。
起始主仓 `29f1b930c5e237ca8b55a65ab3e5ec5be1463701`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

在原八个 K16 步内部使用字面量双 bank 提前加载下一步两个 token tile 的输入；不跨 chunk，不重排各 acc1 的 K 次序，原 Q0/激活/Q2/量化/尾界保持。区别于 EXP-099 的步长特化和 EXP-131 的末段首步权重预取。

Disabled SPIR-V 精确复现继承的 EXP-131 基线。目标 FP32 整网 11,470,640 位模式精确一致。driver .text均31616 B，静态行4161→4160，s_wait_loadcnt72→71；b128=72/b64=16/b32=32保持；VGPR189/SGPR23、零LDS/scratch。实际指令位置证明SHA256：`fdc80422d2df013013c31c85de24042e6dbd754fc1a5674fc63bda86537adb68`。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.028914 / 11.008496 / 11.010935 / 11.028190 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

driver 两版均在首个 WMMA 前加载全部16个 Stage-1 activation b64；没有实现下一 K 输入加载与当前 WMMA 的预期交错。新流程在 ISA 门槛关闭，未启动完整性能 ABBA 或4000帧候选预热；不把静态少一条 wait 换算为收益。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
仅完成disabled构建、目标FP32和driver ISA/资源检查；没有packed候选回读、完整性能测量或其它格式/边界/history/replay/中间值/生命周期验收。静态一致不证明全部输入等价。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / ffwd3w SPIR-V：`2d3400f917303b26afc3290d6c52aada81fca9c44f6f0665ce60598316f94b5a`。
- lookahead / ffwd3w SPIR-V：`53cb437d6cc9b7d1ac3cff5b017456fc6379748420bf2a9fd021630425cade83`。
- prototype.patch：`93bca6becf68d61b9350d41543eced12ad5f6016a47bea683ee0956281c5cef8`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查高成本 C32 image post 的读写地址和输出合成，优先找不依赖调度提示的整数地址消除。
