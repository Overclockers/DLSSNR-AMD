# EXP-143：ViT 扩张第二 K 步延迟 prime

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261010-210227-efficient-five9070xt` 第 3/5 个假设；1 版实现，0 次追加确认。
起始主仓 `b2338215ea5addc0453b811d397ca5a54051097d`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

KFRAG=2原版先prime两个完整操作数bank，再计算首步16个独立WMMA。候选prime首步、计算前两个token行、prime次步、计算余下首步，再完整次步；每个acc仍递增K且舍入位置不变。不跨chunk，不交换AB或独立输出嵌套；区别于EXP-055/105/134。

disabled SPIR-V精确一致；目标FP32整网11,470,640位模式一致。driver .text均13312 B、静态行1720、VGPR157/SGPR20、零LDS/scratch。仅三条WMMA/wait位置交换；两版全部12个数据加载指令位置及操作数完全相同，基线已交错部分加载和WMMA，候选没有进一步延迟第二bank。证明SHA256 `464991a1de5add8c1f5c06b635047bc9903ac9c5340fa62386cc0ec3ad44cb2f`。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.028914 / 11.008496 / 11.010935 / 11.028190 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

机制在ISA门槛被证伪，未进入packed候选回读、4000帧候选预热或完整性能ABBA。三个位置改变不构成加载与计算交错证据，mechanism_not_realized，不保留。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
仅disabled、目标FP32和driver ISA/资源检查；未运行packed候选、性能、其他格式/尺寸、history/replay、中间值、fallback或生命周期验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- delayed / gemmvact SPIR-V：`5437320c7883dcbe5ad959829c30c5e33862ffe78bcd0dde7d4ab863d4dbd205`。
- disabled / gemmvact SPIR-V：`d2f933764c49db726f35d45f7c8cff8cabbf75b9842d28d896e98499d910a9d2`。
- prototype.patch：`b198aaf5a77bf297a1eb5f7ed6fe9fe8670d0d10f5181f988bb2c80aff552df3`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查ViT完整key块的probability denominator跨lane重构：在key permutation下仅交换实际需要的两个packed half部分和，保持原加法顺序。
