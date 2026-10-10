# EXP-161：输入缓存噪声两 dword 的对齐 8 字节读取

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261011-001700-ten9070xt` 第 6/10 个假设；1 版实现，1 次追加确认。
起始主仓 `4b0c6879b19762495e25dfc645f0a1bb84ff6358`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

保持原读取位置，把相邻两uint经binding4的uvec2只读视图一次读取，再用原unpackHalf2x16与float特征承接。缓存基址与8字节pixel stride保持，零offset计算回退不改。不同于EXP-137的提前读取、EXP-065的直接half传递和EXP-119的重排布局；没有改变NaN quieting或数值边界。普通输入与实际runtime_preencode均重建。

两个disabled模块精确复现。普通整网FP32及所有packed回读精确。普通.text25856 B、指令3509→3506、VGPR124/SGPR45、LDS6144 B/scratch0；融合.text26240 B、3577→3576、VGPR124/SGPR46、同LDS/scratch。两模块原噪声buffer_load_b32和u16确实合并为一个b64。融合加载数量b32 2→1、u16 1→0、b64 32→33。两份实际probe绑定各自shader/ISA/ELF、独立完整输出以及全部shader树哈希后才启动性能。

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
| ab-paired | 10.892501 | 10.895668 | -0.003167 | -0.009728 / 0.000013 / 0.000214 |
| ba | 10.963182 | 10.961822 | 0.001360 | -0.006265 / 0.007521 / 0.002826 |
| confirm-control | 10.956610 | 10.957016 | -0.000406 | 0.000455 / -0.001268 |
| control-paired | 10.933130 | 10.933276 | -0.000146 | 0.000106 / -0.000398 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测候选增加0.003167 ms，三组方向混合；唯一反序确认节省0.001360 ms，组差-0.006265/+0.007521/+0.002826 ms，仍混合且与初测方向相反。控制批保留全部样本，未按其中高波动帧删样或修正。没有可靠可复现收益，按unproven关闭。

ADL 合计 638 个有效样本，核心时钟 602–2786 MHz，edge 34–56°C，hotspot 34–81°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- paired / fswinimagepreds32 SPIR-V：`380a39687aed5e92d70881c1ea59055d40648e995ebac46221ca22b75b2334e9`。
- paired / runtime_preencode SPIR-V：`951118140c0f403563549231027481ff3397704dacca3d59932f2bf073d5fed1`。
- prototype.patch：`db150b396ae0f0040ca17ca537f185c67a6c6f526921252d2c63ff425cb78906`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：测试完整NR合成的original纹理读取能否与model/proxy读取及独立运算重叠；十次完成保存核验后关机。
