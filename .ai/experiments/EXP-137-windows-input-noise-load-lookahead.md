# EXP-137：输入缓存噪声提前读取与延迟解包

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261009-235203-ten9070xt` 第 7/10 个假设；2 版实现，0 次追加确认。
起始主仓 `e735b8181e98474c4bfeb1c3d329f70bf00295b3`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

输入层在纹理/颜色准备之后读缓存噪声。第一版把相同地址的两次源级dword读取及half解包提前到纹理采样前，保持缓存布局、生成器、seed/x/y、噪声关闭时的scalar fallback与所有数值运算。不同于EXP-119的布局变换及EXP-070的post纹理预读。第二版只携带原始uint位值，到原消费处才解包，针对第一版提前wait的实际证据修正。

两个disabled模块（普通pre、runtime_preencode）精确复现SPIR-V；两版standalone FP32一致，第一版全部packed回读一致。普通pre.text均25856 B、静态行3509→3505，VGPR124/SGPR45/LDS6144 B/零scratch保持；融合pre.text均26240 B、静态行3577→3573，VGPR124/SGPR46/相同LDS与零scratch。实际读取为b32与u16：第一版第40/41行发出后，在42/45行立即等待，第92行才纹理采样，没有实现预期重叠。raw修正版在普通及融合两个模块的driver .text都与第一版逐字节相同，资源也相同，故不重复packed/性能测量。身份证明SHA256 `5e65128b386ba8e42f31d3b4b44dc5cacb321fff58df14994e170c2cc7e7b356`，runtime探针仅创建pipeline取ISA，不代替宿主输出验证。

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
| ab-lookahead | 10.933182 | 10.941840 | -0.008658 | -0.006983 / 0.000905 / -0.019895 |
| control-lookahead | 10.945577 | 10.941946 | 0.003631 | -0.002170 / 0.009432 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

预期的加载/纹理或编码重叠未实现，mechanism_not_realized，不保留。第一版已启动的完整ABBA结果节省 -0.008658 ms，方向混合且未建立可靠收益，全部结果保留。针对wait修正后真实代码不变，停止本假设，不消耗追加确认。

ADL 合计 315 个有效样本，核心时钟 608–2740 MHz，edge 37–55°C，hotspot 38–80°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 2 份整网 standalone 数值结果，其中 2 份一致。
第二版只做FP32与两模块ISA/资源检查，未重复packed或性能；本项未扩大到其它格式/尺寸、proxy或中间值、motion/history、replay、fallback与双配方完整集成。未通过机制筛选，不主张已证明所有输入等价。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- lookahead / fswinimagepreds32 SPIR-V：`ec90dbe003d353f2e23a766df91f2bb8464b5c6beab99c4ed5d4e93784e30d38`。
- lookahead / runtime_preencode SPIR-V：`533f299b1414b76ffe84ae284eb39192eb74ee886d1428d9629cb02009ac4183`。
- raw / fswinimagepreds32 SPIR-V：`42e1966c9a2cd6f35dc64993eafba661815810062d76e33726005d6457234df9`。
- raw / runtime_preencode SPIR-V：`bfd76efb71fbb4b52651939ff5862b2fda9086ae0b40d2d3abd7639dd6fae6d6`。
- prototype.patch：`e0147445de7d78cdfa99d4209954800578630ac5ec70c30699da80d255a113a6`。
- raw-carry.patch：`deb63a56311b4b603b1617eb2d70acde0be892d78bc08230a0777ef471aa89e1`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查ViT概率片段直接转为B操作数时，能否在保留原key permutation的条件下减少FP8打包。
