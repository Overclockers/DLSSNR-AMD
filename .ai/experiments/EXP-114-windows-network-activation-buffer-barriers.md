# EXP-114：网络内部屏障限定到 activation buffer

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-005048-lossless9070xt` 第 8/10 个假设；1 版实现，1 次追加确认。
起始主仓 `313d157abd830a84e4b71009141b427710c0a5a8`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

纯缓冲区网络步骤的可写资源位于 activation arena。候选用覆盖 act 全范围的 VkBufferMemoryBarrier 替换符合条件的全局内存屏障；COMPUTE→COMPUTE、原 srcAccessMask（inv-only 时为 0）、SHADER_READ|SHADER_WRITE 目标访问和执行次序均保持，队列族仍忽略。Kernel 保存 image binding 数量；首/末、与图像相邻、temporal/mask、exec-only 路径保留原屏障。nr_graph 绑定及 shader 声明审计确认权重只读，persistent Sync/chain 和 BDA 地址都指向 act。不同于 EXP-005 改变访问类型或 EXP-095 合并 API 调用。按 [Vulkan synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)，buffer 范围限定内存访问范围，原计算阶段执行依赖继续保留。

原版、关闭、候选和仅用于诊断的 trace DLL 均重新编译四个对象，header 宏在全部对象上一致。原/关闭 .text749260 B 完全一致，SHA256 3b11c3a50e51d9b2b054813d7d82820313fc63de21b02ee3373bc687e5e29ce6；候选749820 B，SHA256 16cddfb09cd79d09b724b79834cd008a88956cbd22204099583384c37d105cd0。原型未改 shader 字节。独立 trace 的 7 次录制均为123 steps、120 scoped、3 original、inv=1、exec=0；trace proof SHA256 df0c051d1c2c802da3544d8e56e742c3b8fdbcbea0b9d3c271c8fc079a6400d9。所有目标 packed 输出逐字节一致。诊断 DLL 时间不进入验收，也未从 API 范围推断驱动实际 flush 次数。

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
| ab-scoped | 10.985064 | 10.984763 | 0.000302 | -0.003603 / 0.000328 / 0.004181 |
| ba | 11.020494 | 11.027190 | -0.006696 | -0.020982 / -0.003386 / 0.004280 |
| confirm-control | 11.016615 | 11.012853 | 0.003762 | 0.011242 / -0.003718 |
| control-scoped | 10.992660 | 10.993107 | -0.000447 | 0.003012 / -0.003906 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

unproven，不保留。初测 10.985064→10.984763 ms，只省 0.000302 ms，组差 -0.003603/+0.000328/+0.004181 ms，与同期控制重叠。唯一反序确认 11.020494→11.027190 ms，慢 0.006696 ms，三组仍正负混合，未建立完整路径稳定收益。保留全部批次，不追加确认。

ADL 合计 625 个有效样本，核心时钟 26–2770 MHz，edge 36–57°C，hotspot 37–82°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 45 份宿主回读、8015 个測量帧；全部完整输出与独立基线逐字节比较。
已核验目标同步分支与静态 packed 精确回读；未通过性能筛选，因此未扩大其它格式/边界、目标历史、replay、多队列/在途录制/lifetime、fallback 与两仓完整集成验收。不覆盖 Runtime 的 standalone 未运行。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- prototype.patch：`629b3fb9265fef4aee75dd0538d52609191154e6b382739d74737a370e32b4d7`。
- disabled DLL SHA256：`4c4f79d0181b918438e85fccf72a8546355c4149311999dc2f0988d04658d6c1`；构建源/参数清单 SHA256：`70bb65a94f7bf2a3d1363484fa8d7c44fc8d08860fba8c0b30a222ce7f1a5a3e`。
- original DLL SHA256：`7a1825d43ba756d0641d5bb56ec3df9ee1e1067f12b8757976311754ffa49e1b`；构建源/参数清单 SHA256：`211c5f01f41d0e9671f1ed0e66cc52148c7b641ef9c562fef07b54b015225b79`。
- scoped DLL SHA256：`1e2f64c05949d5e6b3b047b6cb7f69e2aac6d9d58e1e2ae03c41fe01017ef2e0`；构建源/参数清单 SHA256：`2592672fcaeb063f2b1698df9d0cf28777501b9232ca61d15ae07070b3fdb5e8`。
- trace DLL SHA256：`ec6e1c65d137c9a4ac959355bd37923126f9b274f053792772c79db4cdc7d1cd`；构建源/参数清单 SHA256：`ea44a07612694a06a0ff65120274d0f5983872db1a312f727bdd6d53a9f0d55f`。

```text
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：测试 packed compose 尾部内存依赖是否可由既有 frame image 屏障承担，同时保留 COMPUTE→COMPUTE 执行屏障。
