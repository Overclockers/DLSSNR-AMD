# EXP-162：Runtime 合成的 original 纹理提前读取

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（no_codegen_change）。
运行 `20261011-001700-ten9070xt` 第 7/10 个假设；1 版实现，0 次追加确认。
起始主仓 `24fe39e88c0448a10a82deee2f4ccf997b5aa0f6`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

把原texelFetch(keep,xy,0)提前到model_pair之前，各FP32计算仍留原处，LOD/坐标/格式和单texel原地packed所有权不变。不同于EXP-070的网络前融合post读取，本次针对网络后runtime transfer的独立纹理交付。普通与packed两个runtime模块独立构建。

两个disabled SPIR-V精确复现，两份独立packed完整回读与保存基线精确。两模块driver ELF .text分别逐字节完全相同；普通5376 B/888条/33VGPR，packed5376 B/885条/32VGPR，SGPR31、LDS/scratch0保持。ordinary text SHA256 64b6e04d78a6a894d79db23e2efead9080efe7d7adb23f4093ec59dafe3e6b3f，packed 75031c7f87ca7d40d7aa8fa0912b858402acafd31d1c3aa53f962e58aefee757，基线候选相同；无实际texture/load/wait变化。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
本项没有运行 standalone；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.930771 / 10.922131 / 10.921556 / 10.919724 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

按no_codegen_change提前关闭；未启动性能A/B，三帧诊断不能作为收益。

ADL 合计 10 个有效样本，核心时钟 838–2200 MHz，edge 39–41°C，hotspot 40–43°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 2 份宿主回读、6 个測量帧；全部完整输出与独立基线逐字节比较。
仅packed静态全图精确与两种runtime driver机器码相等；未运行普通fallback的输出、其它格式/边界/控制、运动历史、replay和生命周期/无缓存广泛验收；没有本项standalone或性能A/B。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / runtime_transfer SPIR-V：`efd839a430e302878651c77844df80a8a561701d0a510af61c4cf9686e8966b3`。
- disabled / runtime_transfer_packed SPIR-V：`7f25cb1fac16344f1a49d170d51f24297d4c041009400339ed9f61fa888022e0`。
- early / runtime_transfer SPIR-V：`a4e5215224d2996b4d4a1b990b0fe7b548ce6a1b9bb31a4b13ea0ea5bd12a84d`。
- early / runtime_transfer_packed SPIR-V：`ec48019e1b319b0a2bd41c3ad14f375a6173c48fd64e04da40c01577a676da60`。
- prototype.patch：`4893c85ab86177ac47baddd38ca355c71408f31d3a67239a58822aabba3d9239`。

```text
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：测试C512 attention首个概率pair的只读偏置提前加载是否能跨QK计算隐藏等待；十次完成保存核验后关机。
