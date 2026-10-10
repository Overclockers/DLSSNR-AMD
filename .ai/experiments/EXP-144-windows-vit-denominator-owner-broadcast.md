# EXP-144：ViT 分母归约的单半 wave 完成后广播

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261010-210227-efficient-five9070xt` 第 4/5 个假设；3 版实现，1 次追加确认。
起始主仓 `c141f5a87b0ec8b9a68d7d9b9917625dad9f97fa`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

完整key块、原NR_VKPERM路径中，原版两半wave都通过四次选择重构low0/low1/high0/high1。候选先交换low0+low1，在upper half执行原左结合的两次后续加法，再交换完成结果并选择一次。保留两次packed shuffle、原partial/K顺序、尾部守卫及所有概率/context。与EXP-107的携带类型、EXP-138的P片段转换不同。

三版均有独立构建。第一版driver将最后三项结合为first+(own0+own1)，FP32不一致；第二版precise向前传播改变partial链，仍不一致；第三版用uint入参helper限定最终三次half加法的NoContraction范围，FP32恢复精确一致。输出位模式差数：`{"owner": 8602289, "owner-precise": 8602280, "owner-scoped": 0}`。第三版driver .text14976→14848 B、静态行2137→2121，cndmask38→32，packed add96/permlane44不变，VGPR132/SGPR33、零LDS/scratch。CPU 100000组half位模式四元组结构链一致，仅为结构检查，不代替GPU异常输入验收。机制证据SHA256 `9678da29a4f4fde090b87fa851a359864b5c26c20a2b60ad83f07a38c43d0499`。目标FP32与全部packed回读一致后，通过绑定门槛才启动性能。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.028914 / 11.008496 / 11.010935 / 11.028190 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-owner-scoped | 10.967925 | 10.965243 | 0.002682 | -0.007319 / 0.007591 / 0.007774 |
| ba | 11.014669 | 11.001451 | 0.013218 | 0.010330 / 0.013058 / 0.016266 |
| confirm-control | 10.999209 | 11.002215 | -0.003006 | 0.004984 / -0.010995 |
| control-owner-scoped | 10.985757 | 10.976956 | 0.008801 | 0.008034 / 0.009569 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测平均省0.002682 ms，组差-0.007319/+0.007591/+0.007774 ms，同路径控制最大组差0.009569 ms；唯一反序确认省0.013218 ms，组差+0.010330/+0.013058/+0.016266 ms，对应控制最大绝对组差0.010995 ms。确认表现改善，但初测不可分辨，确认最小组仍与控制相当；在一个确认额度内未建立可重复区分于控制的收益，unproven，不保留。

ADL 合计 589 个有效样本，核心时钟 68–2729 MHz，edge 37–57°C，hotspot 37–82°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 3 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- owner / vitattn SPIR-V：`b15b59b0e4e7c54654c6a5b447221b061a91608f8b36038ae26902860df9a262`。
- owner-precise / vitattn SPIR-V：`23659257fe6324b644152c8f73e79971dc9ba5af86d2ac8672a3fe1fa588578d`。
- owner-scoped / vitattn SPIR-V：`10ec5128b829847908f8a32aa5a077a65dab90fd4ba5edb4cc8d8ea6696cd2fb`。
- prototype.patch：`39b1dac7abe485793ef0865418b057183b070888b47e7cce71f726a24f4ec1c3`。
- scoped-helper.patch：`352ab22ca4318f5e39487520a8575708136511f81986955225484bf90fb96df8`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查ViT收缩128-channel chunk中原base+scalar offset的每轮地址计算，改用独立片段指针按原chunk步长递增；要求ISA减少地址指令并保持逐项加载和MMA顺序。
