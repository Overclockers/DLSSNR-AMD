# EXP-115：Packed compose 尾部重复内存依赖

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-005048-lossless9070xt` 第 9/10 个假设；1 版实现，1 次追加确认。
起始主仓 `eec66af199b11ab3377cf5a2b0ac2dae008ae2ca`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

direct_packed 合成只通过 result_full 写 frame.image，随后原全局 compute memory barrier 与最终 frame image barrier 可能重复缓存工作。候选只把此处改用既有 exec-only 形式，保留 COMPUTE→COMPUTE 执行依赖（包括后续计算覆盖前的读完成顺序）、最后网络完整屏障以及原 final image 屏障。其余输出路径仍为完整内存屏障；shader、数值、布局、最终 access/stage 掩码均不变。与 EXP-095 API 调用合并不同，本项去掉的是直接 packed 路线上的重复全局内存访问范围，并未删除执行屏障。依据 [Vulkan synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html) 的执行/内存依赖区分，审计所有写入及两个屏障间的尾部命令。

原、disabled、tail 和独立 trace DLL 均重编四个对象。原版 .text749260 B 与继承 DLL 逐字节相同；disabled 同为749260 B但有21字节不同，dumpbin 对200684条指令的比较确认，仅21条指令的数据地址按固定映射交换，操作码、寄存器、指令地址及控制流目标完全一致；没有谎称 raw .text 相同。该比较 proof SHA256 033fe843c056350f75f229789fb45caca69ed420a4ea6afb8c31ade43aca86b6。候选 .text749292 B，SHA256 8f1ce4821521b4e8454d9a87cc80e297c79b885857e944250450c9ea76fa139d。三种正常 DLL 的目标 packed 回读均精确相同。trace 的7次录制均 direct_packed=1、exec-only=1，最终交接保留 GENERAL→GENERAL、COMPUTE/SHADER_WRITE→ALL_COMMANDS/MEMORY_READ|MEMORY_WRITE；最初解析器误把外层 transfer 交接当作 ColourFrame 的内部默认值，查 nr_runtime.hpp 后仅修正解析断言，原日志/输出保留且没有重跑来替换数据。trace proof SHA256 31c26c2a810a7ed87c6c1de1770359aafc01a7eb2c50298b99b6b0831498d9fe。最终 image barrier 源块哈希50152ee891b5cbbbfea611cb0c77f492aaaa7423e588dbb95d7336ee36f608c9，前后完全相同；所有 shader 资产未改。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
本项修改 Runtime 录制路径，未运行不覆盖该路径的 standalone；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.992494 / 10.960786 / 10.984746 / 10.959362 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-tail | 10.980515 | 10.969839 | 0.010676 | 0.019090 / 0.016193 / -0.003257 |
| ba | 11.008103 | 11.007964 | 0.000138 | -0.006776 / 0.010741 / -0.003551 |
| confirm-control | 11.006820 | 11.007550 | -0.000731 | 0.003591 / -0.005053 |
| control-tail | 10.986157 | 10.987696 | -0.001540 | -0.001079 / -0.002000 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

unproven，不保留。首轮完整路径 10.980515→10.969839 ms，表面省 0.010676 ms，但第三组反向。唯一反序确认 11.008103→11.007965 ms，仅省 0.000138 ms，组差 -0.006776/+0.010741/-0.003551 ms，首轮收益未复现。保留全部样本、关闭该假设，不根据单个有利组保留，也不追加第三轮。

ADL 合计 625 个有效样本，核心时钟 60–2793 MHz，edge 34–56°C，hotspot 34–81°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 46 份宿主回读、8018 个測量帧；全部完整输出与独立基线逐字节比较。
已核验目标实际同步路线及原/disabled/candidate packed 精确输出；性能未通过筛选，未扩大其它格式/边界、运动历史、replay、在途录制/多队列/lifetime、fallback 和两仓完整集成验收。不覆盖 Runtime 的 standalone 未执行。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- prototype.patch：`26bd997de86c2c929a9c206c99275fb8ff3dde221bc59583543200ef5feb597f`。
- disabled DLL SHA256：`5783b65754dff78efd8cf05fb1c0d30b9ae565d3b878f3565e44c15a757ab54f`；构建源/参数清单 SHA256：`33421ba67269fc502f1f637c3dc7fdd43566e2b8873610a64af19e2ba636a94e`。
- original DLL SHA256：`e7ebefefdcbb2837836802c2fee11df50bbee487e4e72fe62567a68ce502fba0`；构建源/参数清单 SHA256：`58e4e34b3cb7217c885a52d4de2b683397203d1547c39b365350bf4108b570e8`。
- tail DLL SHA256：`3aa531c0fabc82371582072361988e0b653a2071a29e287f46611a82b0bb707c`；构建源/参数清单 SHA256：`7208466d4f2b1ae42d63b20b079bc655b11e13958b01a6e720fb3c8557a33c59`。
- trace DLL SHA256：`496d0e8e0192a88fc7f38231963c978397682af332673aa3a0a8acfcb7bfe072`；构建源/参数清单 SHA256：`6006b78e59863768ccb66e5ff8a9d083965b10357b547d8e2adc02198f6e4710`。

```text
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：最后一次尝试：在 ViT 完整键块中先计算两组独立 logits，再按原顺序执行各自的概率、分母和 context，尾块仍用原路径。
