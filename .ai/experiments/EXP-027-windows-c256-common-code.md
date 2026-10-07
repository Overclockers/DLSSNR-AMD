# EXP-027：C256 DS 公共代码、追踪解码与实际 wave 驻留

日期：2026-10-07。状态：**研究完成；两个原型均未保留；没有确认新的生产收益**。
这是用户要求继续 EXP-026 的定向研究，不恢复此前已经结束的优化运行或剩余额度。

## 结论

**可以在不增加寄存器、没有 scratch 的条件下缩小 C256 DS 机器码，但本轮没有证明它能稳定加速完整宿主。**
把五种边缘 body 中相同的下采样投影合并为一份，实际 ELF `.text` 从 224,384 降到 195,584 字节，
减少 **12.835%**。普通 standalone 的该 dispatch 三组均快约 0.6%～0.7%，完整宿主初测却略慢，
反序确认的小幅改善又落在基线对照波动内。不能把代码缩减或单内核改善当成完整路径验收。

新硬件计数器中，候选的指令缓存命中率与基线范围重叠，**没有建立“静态体积减少 → 取指改善 → 整网加速”的证据链**。
EXP-026 的低命中信号仍值得调查，但应同时关注动态执行区域及寄存器占用，不继续仅按总代码大小筛选。

另一个有效发现来自已保存的无详细指令捕获：在 shader engine 0 的八个 WGP 中，
C256 DS 平均存活 wave 数为 **15.69～15.82/WGP**，C256 UPS 为 **30.46～30.51/WGP**。
它们的静态 VGPR 分别为 229 / 192，LDS 均为 32 KiB。该相关性支持后续研究寄存器存活期与实际驻留，
尚未证明 VGPR 是全部差异的原因。存活 wave 包括等待，不等于正在发射指令。

## 范围和身份

- 主仓 `ecbd12b`、只读宿主 `3a88c92`。复核 EXP-026 的宿主源码身份后建立独立快照。
- RX 9070 XT、AMD proprietary / 26.8.1 (LLPC)；沿用已核验的驱动环境。
- 输入 2258×1270，工作尺寸 2368×1280，800 ViT tokens，123 个网络 dispatch。
  100% 模型尺度、单次 NR、静态照片、没有运动历史，SR/FG 关闭。
- 探针和单内核筛选用 standalone FP32；完整宿主用 R11G11B10、depth=3、无诊断 production DLL。
  宿主区间从 D3D12 输入复制计至输出复制；没有把它与 standalone 或 RDP 捕获时间混合比较。
- glslang 16.5.0；实际宿主 recipe，包括五个边缘专用 body。
  原型只替换 `g_fswinpds256.spv`，其余着色器逐文件相同。
- 私有证据位于忽略的 `build/windows/swin-code-study-20261007/`；模型读取既有私有资产，
  宿主测试资产用硬链接复用模型。机器路径只在 `.ai/local/` 和忽略产物中记录。
- 本轮没有持续记录温度、频率和后台 GPU 负载。RDP 四次成功捕获日志均确认临时 peak clocks 恢复；
  普通 A/B 不运行 RDP。批次间存在明显慢变漂移，基线对照是判断小差异的必要依据。

## 解码器：有效观测和未解决的边界

用户明确授权下载有助研究的官方 AMD 解码器。GitHub API 限流、稀疏 Git 连接中断后，
从官方 archive 下载，仅提取 `projects/rocprof-trace-decoder`，未安装系统组件。

- ROCm/rocm-systems commit：`bc271416c993833903a19246be00cbb76830ecff`，decoder 0.2.2。
- archive SHA-256：`fda64b0a688c89485bb7df1148aad918b69e6897a53226a8d5d5a221bb5949eb`。
- MSVC/CMake 本地构建，关闭 COMGR/LLVM disassembly 后端；源码逐文件保持下载时身份。
  自定义 ISA callback 使用现有 AMD LLVM objdump 对捕获内 ELF 的反汇编，逐条验证机器字节。
- 原始详细指令流的 shader engine 0 虽返回 API success，却出现未知 token、异常巨大时间戳和
  62 个未映射地址；输出还包含 incomplete/data-lost 提示。**这批结果不作为热点、时延或真实丢包证据**。
  独立 bit-offset 检查也发现格式不匹配；简单按字节对齐不能解决。没有猜测 token 语义或修补后冒充可靠结果。
- 同一捕获的无详细指令 engine，以及两份 counters-only 捕获，可以正常读取 dispatch、
  realtime 和 wave start/end。后两份用于下表，解析器无 error/info 告警。

两个有效捕获各识别 125 个 dispatch：两个驱动内部事件，随后 123 个网络事件。
这 123 个代码对象顺序与 Vulkan 表完全对应；用 SQTT realtime/shader clock 对映插值后，
逐个验证其落在第 99 帧相应 Vulkan 区间内。
wave slot 的 start/end 配对完整，没有重叠占用或遗留未结束项；wave 寿命也检查在所属 dispatch 区间内。
解码器未给这些 Windows occupancy 项填入可用 PC，因此 **wave 按已验证的时间区间归属内核**，不伪称逐 wave PC 归因。

| 内核 | 每次 SE0 waves | 平均存活 waves/WGP，两次范围 | wave 寿命中位数 μs，两次范围 |
| --- | ---: | ---: | ---: |
| C64 DS | 256 | 31.02～31.20 | 730.56～732.02 |
| C128 DS | 256 | 23.49～23.53 | 820.65～823.10 |
| C256 DS | 3,192 | **15.69～15.82** | 37.63～37.76 |
| C256 UPS | 3,192 | **30.46～30.51** | 58.44～58.45 |
| C128 UPS | 256 | 31.30～31.32 | 716.51～718.91 |
| C64 UPS | 256 | 27.57～27.66 | 696.32～707.99 |

六个内核的 wave 数都符合 dispatch 网格、每组 waves 和四个 shader engine 的分配。
C256 DS 在八个 WGP 上平均约 95.95%～96.95% 的观测区间恰有 16 个存活 waves；
切换交界可短暂超过 16，不能把它描述成硬件绝对上限。
C64/C128 是长期 persistent wave，C256 为每组单 item 配方，表中寿命不能跨这些形状直接当作任务时间比较。
这里没有证明 oldest-first 因果关系，也没有获得可信的逐条指令热点。

## 分阶段探针的教训

私有 opt-in 探针按唯一 item 和 wave 分配固定日志槽，无原子日志分配，不增加 barrier，采样 1/8 items。
九个边界覆盖输入、扩张/激活、middle、contract、QKV、attention、context、输出；
第二版缩小到扩张前后两个时间点。禁用时目标 SPIR-V 与独立基线逐字节相同。

但两版都让实际寄存器达到 256 VGPR，并产生 **496 字节 scratch**。
完整探针的 199 个 sampled items × 8 waves 时间顺序及静态输出检查通过，
仍不足以把其阶段比例用于原路径瓶颈归因。两版均停止使用，不报告它们的阶段占比作为结果。
其中开启 `NR_PROF` 的逐层表仍是最后额外采集帧，遵循 EXP-026 已记录的计时边界。

## 两个代码生成原型

### 1. Expansion K 局部循环：未通过资源门槛

保持原 `EXPAND_GROUP=4`、完整 token 分工、五种边缘 body 及所有 private 数组常量索引；
只把 expansion 的 K 遍历改成每次四段的循环，每个累加器继续按 k=0、1、2……顺序计算。
没有复用 EXP-020 的 ESPLIT 或改变独立输出块大小。

### 2. 公共 downsample 投影：代码门槛通过，性能未确认

末端 downsample 投影不读取 `NR_LIVE`，只依赖各 body 已写好的 pooled LDS、统一参数和 wave/head。
在五种 body 的分支合流后执行一份相同代码；保留 `nr_ds_layer` 条件、原 barrier、
权重读取、ascending K、FP16→FP8 舍入、padding 和输出地址计算。
没有减少模型运算、pass、边缘输出或实际每个任务应执行的 WMMA。

| 实际驱动产物 | 基线 | Expansion K 循环 | 公共 DS |
| --- | ---: | ---: | ---: |
| ELF `.text` 字节 | 224,384 | 217,344 | **195,584** |
| 静态 ISA 指令行 | 31,956 | 31,443 | 26,794 |
| 静态 WMMA 行 | 4,138 | 2,986 | 3,882 |
| VGPR | 229 | 256 | **229** |
| scratch 字节 | 0 | **704** | **0** |
| LDS 字节 | 32,768 | 32,768 | 32,768 |

Expansion 机器码虽减少 3.137%，但产生 spill，未提升到性能 A/B。
公共 DS 消除四份各 64 条静态 WMMA 的投影副本；它减少的是重复代码，不是每个任务的算术量。
公共 DS 开关关闭后重编 SPIR-V 与基线完全相同。

- 基线 SPIR-V：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- 公共 DS SPIR-V：`a64043c78e51ab038be65ac1ed53431b2d62997aacd5e57f12e3d6881570b825`。
- Expansion 补丁 SHA-256：`bf3360dfd0aa403cd19f2f5f927ee846636bde35e1f4c49ce0f40c288da6cfca`。
- 公共 DS 补丁 SHA-256：`b0b0e81980cba42edbf6e1fd9b32159ac93db21d9b1338a0282192aa74efa65e`。

## 普通计时与硬件对照

每批 100 帧预热、200 帧测量。普通 standalone 关闭所有记录和 profiler，ABBA×3，共 2,400 测量帧。
逐层表在该无 `NR_PROF` 情况下为测量均值，不是 EXP-026 的额外末帧。

| 普通 standalone | 基线 ms | 公共 DS ms | 节省 ms | 三组节省 ms |
| --- | ---: | ---: | ---: | --- |
| C256 DS | 0.745650 | 0.740600 | 0.005050 / 0.677% | 0.005450 / 0.005150 / 0.004550 |
| 全网络 | 10.344034 | 10.334605 | 0.009429 / 0.091% | -0.002487 / 0.016330 / 0.014445 |

四次新的 RDP counters-only 捕获采用 baseline/candidate/candidate/baseline 次序，
沿用 4096 cycles 采样、完整采样区间纳入和 hits/requests 汇总方法。
每次 31 个派生计数器代数检查及 123 dispatch 时间戳对齐均通过。

| 捕获 | C256 DS 指令命中率 | 指令 requests | 捕获下该段 μs |
| --- | ---: | ---: | ---: |
| baseline 1 | 43.671% | 4,112,782 | 948.20 |
| candidate 1 | 45.226% | 4,094,421 | 969.24 |
| candidate 2 | 46.013% | 4,115,599 | 959.76 |
| baseline 2 | 46.012% | 4,168,399 | 954.92 |

命中率改善未超出基线变化范围；捕获下候选时间也没有优势。
这些捕获存在观测开销及临时时钟设置，只用于机制诊断，不能与普通 A/B 毫秒数混减。
首次启动脚本误把缓冲中的 RDP stdout 当作未初始化，未启动 GPU 程序；修正存活检查后四次捕获成功。

## 完整宿主：32 批，6,400 测量帧

初测 ABBA×3，基线与基线对照 ABBA×2，再做一次 BAAB×3 反序确认。
所有批次使用相同 production DLL、照片、R11G11B10、目标尺寸及 depth=3。

| 比较 | 基线 ms | 候选/对照 B ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| 初测 | 10.782602 | 10.784277 | **-0.001675** | 0.011015 / -0.025438 / 0.009396 |
| baseline 对 baseline | 10.848835 | 10.861324 | -0.012489 | 0.004663 / -0.029642 |
| 反序确认 | 10.898742 | 10.885270 | **0.013472** | 0.029456 / 0.010000 / 0.000960 |

初测和确认方向不同；确认结果与对照变化量同级，且第三组改善降至约 0.001 ms。
本轮判为 **unproven，不保留生产变更**。不挑选确认阶段的正数宣称提速。

## 正确性和交付边界

本轮共 53 次 GPU 程序调用：**21 份 standalone FP32 和 32 份完整宿主 packed 回读全部与独立基线逐字节一致**。
包括诊断、两个原型、普通 A/B 和四次硬件捕获；失败的工具启动未计作 GPU 程序调用。
本轮未扩大到中间值、其他尺寸、真实 motion/history 或游戏，因为性能未通过。
照片一致不替代完整质量验收；没有制作可交付有损功能。

主仓仅研究文档、索引和状态更新。生产 shader、Linux、宿主源、DLL、部署和发布配置均未改；
末尾复核宿主源快照身份相同。本次仅提交研究记录及索引/状态；没有推送、安装服务、游戏、FSR4/FG 或发布。
原有 `.codegraph/` 未触碰。所有工具、源码包、模型引用、捕获和输出继续留在忽略目录。

## 复现和后续

私有脚本：`study.py` / `analyze.py`、`expand.py` / `common_ds.py` / `inspect_code.py`、
`compare.py` / `host_compare.py`、`capture.py` / `correlate_counters.py`、
`fetch_archive.py` / `decode_trace.py` / `peek_tokens.py` / `occupancy.py` / `audit.py`。
首次 setup、构建、测量均拒绝覆盖已有证据；不要直接重跑整个流程。

CPU 复核：

```powershell
python -X utf8 build/windows/swin-code-study-20261007/occupancy.py timed-counters-1
python -X utf8 build/windows/swin-code-study-20261007/compare.py summary
python -X utf8 build/windows/swin-code-study-20261007/audit.py
```

完整宿主调用格式：

```text
profile_host.exe <saved-production-dll> <baseline-or-candidate-assets> <csv> 3 0 200 100 2258 1270 <packed-output> <photo>
```

后续不要重复当前公共 DS 或 expansion K 参数扫描。优先解决详细 SQTT 的格式兼容，
或者设计不引入额外 LDS 重读/归约变化的寄存器存活期干预，并用实际 wave 驻留验证机制。
代码体积门槛仍有用，但已证实不能替代实际执行区域、资源和完整路径性能检查。
post→compose 融合及目标尺寸离线运动历史基准仍是独立方向，本轮未实现。

参考：[EXP-026](EXP-026-windows-swin-bottleneck-profiling.md)、
[官方 decoder 源码及接口](https://github.com/ROCm/rocm-systems/tree/bc271416c993833903a19246be00cbb76830ecff/projects/rocprof-trace-decoder)、
[RGP 指令计时说明](https://gpuopen.com/manuals/rgp_manual/events_windows/)。
