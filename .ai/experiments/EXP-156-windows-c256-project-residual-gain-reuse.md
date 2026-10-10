# EXP-156：C256 DS 输出投影残差增益跨 query 复用

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261011-001700-ten9070xt` 第 1/10 个假设；1 版实现，0 次追加确认。
起始主仓 `e2141ead7e6b10d8be01602e08b47925cc5b04a4`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

输出投影初始化反复使用同一通道对的只读 FP32 ars 增益。候选把独立 c/m 循环转置，每对增益跨原 query fragments 复用；没有 EXP-081 的 wave 广播、scalar 路径改写或额外 lane 选择。各 residual 仍为同一 FP8 解码值乘原 FP32 权重，各 MMA 累加顺序、LDS 地址及同步保持。只构建目标 fswinpds256 的独立 Windows 原型。

关闭宏精确复现基线 SPIR-V；实际候选整网 FP32 的 11,470,640 个值逐字节一致。driver .text 224512 B 不变，静态指令 32006→32004，VGPR229、SGPR107、LDS32768 B、scratch0。所有 memory-load opcode 数量不变；主要变化为 v_add_nc_u32_e32 少2条、v_dual_mul_f32 多1条以及 delay/wait 调整。驱动原本已复用该只读增益，源码复用没有实现计划的读取减少，也没有降低资源峰值。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.930771 / 10.922131 / 10.921556 / 10.919724 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

按 mechanism_not_realized 关闭；没有完整宿主性能 A/B，不声明提速或退化。本轮保留收益仍为0 ms。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
仅完成 disabled SPIR-V 与静态 FP32 整网精确比较；未运行本项 packed 输出、性能 A/B、其它格式、边界尺寸、运动历史、中间值、replay、fallback、两套配方完整集成和无缓存验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / fswinpds256 SPIR-V：`3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad`。
- reused / fswinpds256 SPIR-V：`0274314d1ae5e56cfef0691dcd657886736f4c667affac2e05f36251a84aa8d2`。
- prototype.patch：`11bda5795e83282b34dfdea9f75b675b876895f29d8219c23967fef8acae5b38`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：调查 FFWD Q0 配对权重读取后的独立 MMA 分组，保持每个累加器的 K 顺序。十次完成并保存核验后执行用户指定关机。
