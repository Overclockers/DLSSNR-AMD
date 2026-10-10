# EXP-146：C256 bias 预取与局部锁序隔离

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261010-224000-ten9070xt` 第 1/10 个假设；2 版实现，0 次追加确认。
起始主仓 `2130829e13df9604ff5bb2206e6788801c711f06`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

拟把四组原 FP32 bias 读取提前到当前 query 的 QK MMA 前。首版重复了 EXP-029/123 的失败；开跑前对这两项复核不充分，原型与预算均如实保留，不将其重标为 discovery。随后根据 EXP-144 的 uint 参数局部 NoContraction 新证据，第二版尝试用独立 packed-half 加法 helper 保持现有 driver 归约顺序，避免 precise 回溯改变指数生成。该修正仍属同一假设，不增加计费。

首版 FP32 输出 SHA256 43dfca88efbdc9039fd7461aa99cc70e736b0dd2f7c7cea1706de1819d104f3b，重现旧失败，不能验收。修正版按既有 driver 顺序 1,0,2..15、XOR16、half合并，整网全部位模式精确一致。修正编译曾因保留词 flat 失败，改名后完成；一次误跟随失败构建的 inherited-baseline 调用已保留并标 candidate_valid=false，不作为候选证据。两版 driver .text 均224512B，修正版32006条指令/opcode计数与基线相同，229 VGPR/107 SGPR/32768B LDS/零scratch。5440个buffer-load/WMMA事件的opcode顺序全部相同，加载没有跨越对应矩阵事件；局部隔离未实现新的预取顺序。完整证据和源身份见 mechanism-proof.json、build.json。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.963855 / 10.966668 / 10.968056 / 10.955780 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

mechanism_not_realized，不保留。修正版恢复了静态数值，却没有实现所需加载提前；未启动候选packed检查、4000帧候选预热、完整宿主性能ABBA。指令位置/分配变化不作为GPU收益。

保存 2 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / fswinpds256 SPIR-V：`3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad`。
- early / fswinpds256 SPIR-V：`98c5c4374eea18b8a0b1d36398a6b06d92ba8c77ff23d2d2a160fde71b1f4a8c`。
- ordered-repair / fswinpds256 SPIR-V：`e1b8f5312c588f0a26755aa460b93c2f3259b79406353e5603736280aee306db`。
- ordered-variant.patch：`9275b5046331ce21dfd0658540252a73cc04d5463544978318cc9c499a7777e6`。
- prototype.patch：`969c6c475d7e5fb5bd54c182671916c3171b971c36794346dc201f48391d285b`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续第2/10项：检验C512 Q/K精确归一化树的独立子树即时消费，缩短中间值存活期。
