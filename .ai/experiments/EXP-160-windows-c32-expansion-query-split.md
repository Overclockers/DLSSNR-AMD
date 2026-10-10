# EXP-160：C32 扩张按 query 对分批降低累加器峰值

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261011-001700-ten9070xt` 第 5/10 个假设；1 版实现，0 次追加确认。
起始主仓 `201929eca4d62d159f68016489ee1241a3e618e4`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

本次独立诊断C32普通层合计约1.2634 ms，为最高家族。仅该扩张阶段每h先处理m0..1再m2..3；各结果K次序、FP32激活多项式、原量化与后续全部阶段不变。不同于EXP-020的C256 DS与EXP-003的attention query分批。候选计划先检查寄存器峰值及scratch，资源改善未成立不进入性能验收。

disabled精确复现；整网FP32全11,470,640值一致。VGPR189和LDS2048 B不变、SGPR33、scratch0；driver.text38144→38528 B、指令4867→4956，主要增加99条s_delay_alu。分批没有降低完整kernel寄存器峰值或增加驻留能力，虽部分独立FP32运算调度改变，也不能当作占用优化成功。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.930771 / 10.922131 / 10.921556 / 10.919724 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

寄存器/占用机制没有实现，按mechanism_not_realized关闭，不进入完整宿主性能A/B；没有收益结论。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
在寄存器机制检查提前关闭，未运行本项packed输出、性能A/B、其它格式、边界历史、中间值、replay、fallback、两套配方和无缓存验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / fswin32 SPIR-V：`572651948aaf9c0daf2a2ea2f0a3669065b4a6824d24c7d0827cbe290f8a426a`。
- split / fswin32 SPIR-V：`94d87ce3b8ea65875026c3b96adf3ff19120ba4689729ecde37a26ad64bc583c`。
- prototype.patch：`2ad231f4a090e966e9b4d28971af44c78ff34434459c41a394be8bc84afb1fe2`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查输入缓存噪声两dword是否能通过8字节视图合并读取；保持原half-float转换与分支。完成十次保存核验后关机。
