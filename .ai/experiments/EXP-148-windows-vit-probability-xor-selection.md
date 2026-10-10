# EXP-148：ViT概率片段的xor条件位交换

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（no_codegen_change）。
运行 `20261010-224000-ten9070xt` 第 3/10 个假设；1 版实现，0 次追加确认。
起始主仓 `deab61bce3497f898ad3df36cc75fd3a38630187`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

完整键块NR_VKPERM路径的三个uint条件选择改为xor/and位混合；保留原概率生成、FP8转换、key排列、分母和context所有运算。与EXP-138的直接B转换、EXP-144分母owner不同，本项只测试完成量化后的纯整数选择代码生成。

320,000个uint lane随机映射案例精确通过；xor掩码每一位取原条件选中的对应uint，不引入浮点算术。BDA与nobda的disabled SPIR-V均精确一致，候选目标FP32全图精确一致。实际执行BDA driver .text14976B逐字节一致，全部2137条指令相同，132VGPR/33SGPR/零LDS和scratch不变。编译器把两种条件表达式归一为同一代码。nobda已构建但目标路由未执行，未声称其GPU验收。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.963855 / 10.966668 / 10.968056 / 10.955780 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

no_codegen_change，不保留。实际目标驱动机器码完全相同；未启动packed候选检查、4000帧候选预热或完整性能ABBA。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- disabled / vitattnnobda SPIR-V：`3cdd38d4115abe4c9c063dd519cecd641ee9bc93956745163cf7f8e502d419ff`。
- xor / vitattn SPIR-V：`ac27fde349a93fcd46c546f52154111f39c5fbb9beab85cac8ffa75b07f72210`。
- xor / vitattnnobda SPIR-V：`bfe2cec8f5918890388a76c8a43ba8c5e4e9f13f2489823d8ea70b130e722ea2`。
- prototype.patch：`d8715754dc399f8a3326cce2458a1b696cfc1566e7fda96005c7b1aa101161bd`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续第4/10项：FFWD已完成middle累加器的逐K激活转换与即时final消费，避免重复EXP-073的middle重分组。
