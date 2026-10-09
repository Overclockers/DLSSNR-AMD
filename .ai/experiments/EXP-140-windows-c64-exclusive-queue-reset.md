# EXP-140：C64 persistent 队列的独占结束复位

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-235203-ten9070xt` 第 10/10 个假设；2 版实现，1 次追加确认。
起始主仓 `d400d6f295531882cb5f2facec904954343f6dd0`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

仅在 fswinpds64/fswinpup64 用最后完成工作组的 coherent 普通写入替代 head/finished/tail 三个 atomicExchange，保留完成 atomicAdd 和 epoch atomicExchange。与 EXP-085 的并发轮询读取不同，本项针对独占复位写入。限定 PBATCH=1、PERSIST_ONE=0、STRAG=0；其它组合编译期拒绝。C64 每次 enqueue 后有工作组屏障，最终无效 claim 经过 buffer 发布和两处工作组屏障才退出；所有工作组完成计数后，不再有 head/finished/tail 访问。后续 dispatch/frame 沿用既有同步。C128/C256 的 ONE 退出不包含在该论证中。

两份 disabled SPIR-V 精确复现基线；两版 FP32 整网和 packed 回读一致，日志没有 persist/chain error。第一版新增 memoryBarrierBuffer，原子swap各5→2、普通store各0→3，但新增global_inv。第二版仅将这处新增屏障改为 device/buffer Release；原有两处 QueueFamily/AcquireRelease 屏障原样保留。SPIR-V 新增范围/语义为[1,68]，driver确认最后store→s_wait_storecnt 0→原子epoch，消除第一版多余global_inv。最终DS .text56960 B、指令8038→8039；UP .text50816 B、指令6938→6939；VGPR192/194、SGPR98/88、LDS均8192、scratch均0。同步审计SHA256：`9fb1c191c9fdd665208577251abf5c2cb2faafe796b6d01133307d50e4c7dca7`。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.943105 / 10.917341 / 10.917457 / 10.944389 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-release | 10.956480 | 10.951784 | 0.004696 | 0.007749 / 0.003342 / 0.002997 |
| ab-stores | 10.949599 | 10.949460 | 0.000139 | 0.005798 / -0.000803 / -0.004579 |
| ba | 10.977008 | 10.974138 | 0.002870 | 0.005857 / -0.002209 / 0.004961 |
| confirm-control | 10.964676 | 10.965438 | -0.000762 | -0.002474 / 0.000950 |
| control-release | 10.951999 | 10.953066 | -0.001067 | -0.000926 / -0.001208 |
| control-stores | 10.957262 | 10.952413 | 0.004849 | 0.010727 / -0.001030 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

第一版完整路径平均节省0.000139 ms，小于控制波动。release版初测节省 0.004696 ms，三组为正；唯一反序确认节省 0.002870 ms，但组间正负混合，确认控制最大绝对组差 0.002474 ms。两批均值均为正，但反序一组慢0.002209 ms，未满足本轮 EXP-131 使用的每组收益均超过对应控制最大绝对组差的保留判据。追加确认额度已用完，保守记为 unproven，不保留；不声称该机制绝无收益。

ADL 合计 929 个有效样本，核心时钟 0–2740 MHz，edge 39–57°C，hotspot 40–82°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 67 份宿主回读、12021 个測量帧；全部完整输出与独立基线逐字节比较。
保存 2 份整网 standalone 数值结果，其中 2 份一致。
未通过性能验收，未扩大到两配方、其它格式/边界尺寸、目标运动历史、逐帧队列状态、中间值、replay和完整生命周期验证。静态一致、源码独占论证和当前错误标志检查不替代上述保留条件。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpup64 SPIR-V：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- release / fswinpds64 SPIR-V：`f05f69850c727d7b0cf6dbcb70f1a2c08273999594a0e4079fc6121f1694a74b`。
- release / fswinpup64 SPIR-V：`23fd3605121fdaa06a84668a345b4e7d4903af17d8c1bd6d68e2d0b1019b19cc`。
- stores / fswinpds64 SPIR-V：`96142700b2af3a90a20db162baeef2577f3da517c96b757105a282d91b862873`。
- stores / fswinpup64 SPIR-V：`70f751a10cc136435151ade857afcc9175015b36845679059a4a8657567fe5d4`。
- prototype.patch：`ec9a8f7a38b4045c17c4f1aeec1d3369f6d0afe374f9efa7e9def17c5757f6b8`。
- release-refinement.patch：`4732733229fd664eb423896b8903f657f0ce6977421dbee8d43128f303f21f3a`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮 10/10 已达目标，停止新增假设；核验最终提交、产物与远端，保存总结后按用户要求执行 shutdown -s -t 0。
