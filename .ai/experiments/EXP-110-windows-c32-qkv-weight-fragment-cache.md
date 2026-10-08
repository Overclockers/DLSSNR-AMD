# EXP-110：C32 QKV 的权重片段寄存器复用

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261009-005048-lossless9070xt` 第 4/10 个假设；1 版实现，0 次追加确认。
起始主仓 `5a2f791b7bdad8ec225bf5ee147eedcfd419c047`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

C32 非 head-wave QKV 的 m 循环重复引用同一组 6×2 个权重片段。候选在 m 循环前构造原 Q/K A 片段和 V B 片段，按原类型与方向复用；K 次序、零初值、归一化、所有舍入与写入不变。仅三个普通/DS/UPS C32 模块，不改 image pre/post。与 EXP-071 输入 lift 权重、EXP-097 C512 K/V 缓存不同。原型脚本一次替换范围断言、一次展开后死分支负索引构建错误均已修正；实际一次实现变体，失败材料保留。

三个 disabled 模块精确复现基线。候选目标 FP32 和 packed 全图一致。三个模块的所有 load/store opcode 数量均不变，说明显式缓存未减少预期的重复读取；驱动本已复用。代码字节数各为 38144/46976/42240，前后相同但机器码非逐字节相同。静态指令分别 4867→4863、6428→6430、5467→5465，变化局限少量 MOV/VALU/调度；VGPR 189/183/190，零 scratch，与基线资源相同。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.992494 / 10.960786 / 10.984746 / 10.959362 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-cached-fixed | 11.138871 | 10.973824 | 0.165048 | 0.491949 / -0.007182 / 0.010376 |
| control-cached-fixed | 10.980107 | 11.224721 | -0.244614 | -0.005509 / -0.483720 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

mechanism_not_realized，不保留。预先规定的“减少权重读取”机制未实现，因此不为零散调度差异扩大验收或追加确认。已启动的一轮 ABBA 和控制完整保存：表面省 0.165048 ms，但第一组受基线尖峰主导，后两组正负混合；同路径控制差 -0.244614 ms，同样有尖峰。这些数据不能证明提速，也没有据此保留候选。

ADL 合计 327 个有效样本，核心时钟 55–2734 MHz，edge 35–53°C，hotspot 36–79°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- cached-fixed / fswin32 SPIR-V：`6170c7f399228f066a33f81339eb8ebd518f4198c1e7ae73199083d12abcb6ef`。
- cached-fixed / fswindsp32 SPIR-V：`6e03d806d422f069bcbf5dfe1f16521cbdb86909f8cd47c51b7c26f6a6c2c630`。
- cached-fixed / fswinfusedup32 SPIR-V：`ff50642a98f83c3ffa6ca7535964f9ce52338bdeea85aa379181a687cc5d279d`。
- disabled / fswin32 SPIR-V：`572651948aaf9c0daf2a2ea2f0a3669065b4a6824d24c7d0827cbe290f8a426a`。
- disabled / fswindsp32 SPIR-V：`1287c97821396cc94440153887242fac8b9b236eae8c351962356cca56d20baf`。
- disabled / fswinfusedup32 SPIR-V：`a470ad407749a336b3ac7a721a23fc285769d51976424a9490784e535cfcd243`。
- final-prototype.patch：`d7e264662c4de9028cf396364e4f90159380f10338b9175115172300819eb1ee`。
- index-repair.patch：`0bf34423d62e6123a71a1ed274590327bab087987768b13d325ce4a7ea85aa7f`。
- prototype.patch：`78af286c2ec2ad86d260fd4de30ce3249f839a633e5318e9642660becfa879c9`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：把 fused input 的原 GPU 有序控制参数更新移到更早的位置，检查能否减少网络开始前的传输等待。
