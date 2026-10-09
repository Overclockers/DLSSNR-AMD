# EXP-120：Packed compose 单独采用 wave64

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-085523-newpaths9070xt` 第 4/5 个假设；1 版实现，1 次追加确认。
起始主仓 `86e233c86d3df92ad67f1d7eba153f4573f6668c`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

只为runtime_transfer_packed.spv请求64-lane subgroup；查询min/max及compute支持后选择，能力不足保留32。原8×8工作组、派发、SPIR-V、像素公式和其它管线都不变。宿主已启用subgroupSizeControl；没有增加full-subgroup标志。不同于EXP-040的工作组形状及EXP-059的完整subgroup编译契约。合法性按[Khronos subgroup-size规范](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineShaderStageRequiredSubgroupSizeCreateInfo.html)核对。

原版、disabled和wave64的四个宿主对象各自重编译。正式基线采用与继承production DLL的.text逐字节一致的disabled；额外original构建的.text有21字节RIP-relative引用偏移差，证据保留，未用于计时。探针请求32/64分别生成32/64位EXEC控制指令；driver executable property却都报告64，因此不能单凭该属性判定wave大小。两版机器码5376 B，静态指令885→929，VGPR均32、SGPR31→34，均无LDS/scratch。格式转换131841像素零差异；两输入格式×四white point×五controls共40个合成数组、1326120个packed值精确一致；目标完整宿主输出也完全一致。wave-proof SHA256 631b319bc85cc6b3244940fc94903a56dd86b22f0493fbd4c86a5640203c9b73。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
本项修改 Runtime 录制路径，未运行不覆盖该路径的 standalone；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.978340 / 11.019070 / 11.008404 / 11.006539 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-wave64 | 11.055182 | 11.050577 | 0.004605 | 0.003701 / 0.006455 / 0.003659 |
| ba | 11.103374 | 11.099824 | 0.003550 | 0.000381 / 0.005958 / 0.004312 |
| confirm-control | 11.100518 | 11.086512 | 0.014006 | 0.007551 / 0.020462 |
| control-wave64 | 11.072808 | 11.071685 | 0.001122 | 0.002916 / -0.000672 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

unproven，不保留。初测11.055182→11.050577 ms，省0.004605 ms；唯一反序确认11.103374→11.099824 ms，省0.003550 ms。六组虽方向一致，确认同路径控制本身省0.014006 ms，组差0.007551/0.020462 ms均超过候选效应，无法与波动区分，不追加第三轮。

ADL 合计 590 个有效样本，核心时钟 77–2736 MHz，edge 37–59°C，hotspot 37–84°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
已做独立转换/合成探针和静态目标全图；性能未通过，因此未扩大到非有限payload、更多边界/格式、目标运动历史、replay/多队列/lifetime、缺少wave64设备回退、另一源码树和完整集成验收。未运行不覆盖runtime compose的standalone性能程序。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- prototype.patch：`c6390c17923e0d81d5db55cd48a15e93af5487d26ba7afe68a6d74a124ba900a`。
- disabled DLL SHA256：`c03940c1fbbc31cf1653042e5ab78f280ab9dc48a7fd85bb170fbc820d379010`；构建源/参数清单 SHA256：`2629eebdc77a9343e4d2bab826ff508d11650027c0ff7e8ff7b8d5717fb2f0c0`。
- original DLL SHA256：`3c9e061208d5948a44d3e9195ef734b87cffc48aa3eb2fd66e89384c1764715d`；构建源/参数清单 SHA256：`87501a0ef4d19f70905097da31e16928811c7aa23b902c71b414947051075b22`。
- wave64 DLL SHA256：`eb8f05114adaab8ac2ef522b5ed30f95ebf70686d9263abce317132e5e11d296`；构建源/参数清单 SHA256：`b60b285391c4ebf2dd84714b940876c20152613f53b7f768f1b7936bc609be8c`。

```text
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：按新的地址冲突假设，测试小型activation槽位增加256-byte间隔；保持256-byte对齐、生命期及cold-age规则，并核验实际布局。
