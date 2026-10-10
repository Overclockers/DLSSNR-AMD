# EXP-145：ViT 收缩的片段地址循环携带

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（no_codegen_change）。
运行 `20261010-210227-efficient-five9070xt` 第 5/5 个假设；1 版实现，0 次追加确认。
起始主仓 `caed58b64ce0fda14fbe2842e6712aacb77e281d`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

当前gemmprojws每K128块更新两个scalar偏移，再加入三个lane基址。候选让原activation/成对weight片段地址按各自常量步长递增；K=4096、N=1024、KFRAG=8和PIPE=3保持，不改变片段、WMMA序列或epilogue。区别于EXP-056尺寸特化、EXP-055跨chunk预取。一次原型脚本出现匹配计数断言，源未写入或编译；修正排除XPIPE文本后恢复同一已计费假设，未新增额度。

disabled SPIR-V精确复现基线，目标FP32整网位模式一致。131072项地址递增检查通过。driver .text4992 B逐字节相同，全部742条指令一致，VGPR96/SGPR30、零LDS/scratch。driver .text SHA256 `48e256ad4f7b265269bfabd06ce1aac7909434d114d433b52ed677c174177f77`；机制证明SHA256 `7e0e2715c1a917ac2c9540a8a7fc738feb1536e8dfdb401f32a01f068ee383bb`。SPIR-V变化未传递为实际地址指令优化。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.028914 / 11.008496 / 11.010935 / 11.028190 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

编译器将两种地址表达式正规化为同一程序，no_codegen_change，不保留；未启动packed候选检查、4000帧候选预热或完整性能ABBA。五个已计费假设完成：三项在ISA阶段关闭，两项精确通过但性能收益未证明。本轮新增保留代码/确认GPU毫秒收益均为0。工具已落地：无缓存基线/新builder70文件精确一致，两仓各65个缓存shader与独立clean相同，10项工具测试通过；宿主shader暖缓存壁钟23.228→5.540秒，约少76.1%，此数不含runtime/可执行重建，也不是GPU提速。按阶段保存预处理、构建、数值/ISA和计时壁钟；本轮三项没有触发完整长测。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
仅disabled、目标FP32、地址递增及driver完整.text/ISA/资源检查；未运行packed候选、性能、其它格式/尺寸、history/replay、中间值、fallback或生命周期验收。未保留，无需将静态检查扩充为生产验收结论。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- carried / gemmprojws SPIR-V：`f30c3544a1b91fc8f0b352d84491f5178d3f6fceb58906fc9ea52e4edec5eb8f`。
- disabled / gemmprojws SPIR-V：`63449ee64f848b4d78056350362052a10e279f795dc3788bfee7bc0cb118f636`。
- prototype.patch：`41e93fd139b89c7325062d6caace9b053fc291f6b1afb80f4524b5c1a8630b97`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮5/5预算耗尽，停止（budget_exhausted），不自动启动新运行。后续新预算优先采用独立算子中间值检查与局部NoContraction范围，结合EXP-028已验证加载等待研究；只有完整链路可重复收益才保留。
