# EXP-165：ViT QKV 同 token 边界 guard 共享

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261011-001700-ten9070xt` 第 10/10 个假设；1 版实现，1 次追加确认。
起始主仓 `9d28c36668f911ea91b09bdf62a774f94b9deb40`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

独立开关仅在fused QKV模式下将helper中的tok0>=pc.M早退移为NR_EPI_I中的相同tok0<pc.M谓词，包围原顺序的全部channel pair调用。tok0仍为mg0+mloc+uint(ii)*16u。不改变GEMM、权重、Q/K/V分类、四步subgroup归约、precise half sqrt(32)→norm→temperature顺序、量化或写址。不同于EXP-133的Q/K/V分类外提，本项不复制算术循环。

disabled SPIR-V逐字节重现独立基线；目标静态FP32与packed完整输出全部一致。基线首token在ISA381保存谓词、384..385保存EXEC并分支，在729恢复EXEC后731..732重复相同mask控制；候选384..385一次cmpx/execz覆盖两对channel，直到1054..1055下一token。另三个token基线已共享guard。因此仍有四个token比较，只消除首token重复EXEC分割；s_cbranch_execz5→4、s_and_saveexec2→0。driver.text20224→20096 B、静态指令3041→3019，VGPR167/SGPR23、LDS/scratch0保持；浮点/WMMA/加载/存储opcode数量不变，分类vccnz59→60，不能把单项减少解释为全路径收益。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.930771 / 10.922131 / 10.921556 / 10.919724 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-shared | 10.928775 | 10.931767 | -0.002992 | -0.002712 / -0.004881 / -0.001382 |
| ba | 10.973998 | 10.973693 | 0.000304 | 0.001257 / -0.004657 / 0.004314 |
| confirm-control | 10.963599 | 10.964922 | -0.001323 | -0.006794 / 0.004148 |
| control-shared | 10.936434 | 10.937637 | -0.001203 | -0.000917 / -0.001489 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

完整离线路径初测10.928775→10.931767 ms，节省-0.002992 ms，三组都轻微反向；同路径控制平均-0.001203 ms。唯一反序确认10.973998→10.973693 ms，节省0.000304 ms，三组+0.001257/-0.004657/+0.004314 ms，方向混合且落入控制波动。未建立可重复完整路径收益，按unproven关闭；没有保留生产改动，本轮新增保留收益0 ms。

ADL 合计 590 个有效样本，核心时钟 0–2744 MHz，edge 34–56°C，hotspot 35–82°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
已完成disabled、目标静态FP32与packed精确输出、driver ISA机制审阅及ABBA/同路径和唯一BAAB/反序同路径控制；未扩大到其它格式、边界尺寸、运动历史、中间值、replay、fallback、资源生命周期或两套无缓存集成验收。静态一致性不证明所有输入等价；性能未证实，因此不进入保留验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / gemmvqkvnorm SPIR-V：`7e3251e0d1cddac21a28f55578c40be707e864327222d64d82dfca16ab9aa0eb`。
- shared / gemmvqkvnorm SPIR-V：`11a97c8cc5831a188c9a88633d2c8ff6fdec7d8c5bd59d927434be3d9b390b0f`。
- prototype.patch：`eb1090d8ed402f6aa211bd006ec3d559bb76570398316e61b3eb309b6cfaeebc`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮十次额度耗尽；关闭和提交本项，推送核心main-experiment，核验全部十项提交、源/产物身份、保护分支和预存暂存项，保存最终报告后执行用户指定shutdown -s -t 0。
