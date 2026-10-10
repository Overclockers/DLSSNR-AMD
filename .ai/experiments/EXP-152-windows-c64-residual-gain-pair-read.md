# EXP-152：C64残差gain的对齐成对读取

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（no_codegen_change）。
运行 `20261010-224000-ten9070xt` 第 7/10 个假设；1 版实现，0 次追加确认。
起始主仓 `743f0bdcd372090216278bfe994876cd534b6f23`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

C64 DS/UPS的FP32残差gain从相邻off/off+1标量读取改为独立readonly vec2视图；偶offset读同一8字节，奇offset保留原两标量回退，原FP32乘法/累加器初始化不变。不用EXP-081的wave广播/选择，也不做EXP-062的提前读取。

65,536个含奇/偶offset的字节地址对精确一致。两个disabled SPIR-V精确复现基线；目标整网FP32逐字节一致。DS64 driver .text56960B/8038条指令、UPS64 .text50816B/6938条指令均逐字节相同；DS192VGPR/98SGPR、UPS194VGPR/88SGPR、各8192B LDS、零scratch不变。原驱动本来已经生成相同读取与奇地址处理，显式vec2不减少加载。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.963855 / 10.966668 / 10.968056 / 10.955780 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

no_codegen_change，不保留。两个实际目标模块驱动代码全部相同；未启动packed候选检查、4000帧预热或完整性能ABBA。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpup64 SPIR-V：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- pair / fswinpds64 SPIR-V：`07b754467ab487a5ba7ca5d4ab1eb68d77f1cf5ba0670a4fc73e62ffa84ca3ab`。
- pair / fswinpup64 SPIR-V：`a2cf7501bf012c36dc3cc5b2803984ef202e772d17c9eb29dd1a0225a101dde8`。
- prototype.patch：`a6e1638f22ed7d619dab586b1333283675b600993e9076ba314584d6bb00c681`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续第8/10项：现存Windows C256 UPS首先读取全部32个输入片段，检验逐K消费能否缩短该区域存活期。
