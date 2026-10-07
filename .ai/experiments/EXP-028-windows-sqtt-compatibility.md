# EXP-028：恢复详细 SQTT 指令分析并定位 C256 DS 等待

日期：2026-10-07。状态：**离线兼容性研究完成；采样热点有效，尚无新的生产性能结论**。
用户明确通知“可以测试了”后解除此前暂停。本轮执行 CPU 检查、两份隔离 decoder 构建和旧捕获解析，
没有新 GPU 基准或捕获；生产代码及宿主未改。游戏验证仍按已有决策暂缓。

## 研究目标与身份

[EXP-027](EXP-027-windows-c256-common-code.md) 的公共 DS 投影使代码缩小 12.835%，
但完整宿主收益未确认；详细指令流又存在地址和时间异常。本轮先修复观测工具，
取得能够定位加载、计算和同步位置的证据，再选择保持算术的干预。

- 主仓起点 `3d8389f`；宿主沿用 EXP-027 的 `3a88c92`，本轮未重做宿主身份审计。
- 使用已经授权取得的 ROCm/rocm-systems 源快照
  `bc271416c993833903a19246be00cbb76830ecff`、decoder 0.2.2。
  原始 176 个源码文件重新核对哈希，全部未改；补丁和构建均使用独立副本。
- 捕获环境：RX 9070 XT / AMD proprietary 26.8.1 (LLPC)，2258×1270 FP32 输入、
  2368×1280 工作尺寸，123 个网络 dispatch；静态照片、单次 NR、100% 尺度，无历史/SR/FG。
- 详细输入为 EXP-026 的 `timed-hardware.rgp` SE0。对照为同捕获 SE1 和
  `timed-counters-1.rgp` / `timed-counters-2.rgp` SE0。
  旧捕获开启 instruction tracing / counter collection，SQTT buffer 为 `high`。
- 本轮不连接 RDP、不更改 GPU 时钟；时钟恢复证据沿用原捕获日志。
  旧 `baseline-hardware.rgp` 未在本轮扩展分析。
- 全部脚本、源码补丁、DLL 和私有证据位于忽略目录
  `build/windows/sqtt-compat-study-20261007/`；本机位置另记 `.ai/local/`。

## 三处兼容问题与修复范围

### 1. 缺少两个可选执行事件 token

独立前缀检查重现首个 `INST` 在 bit 51120，首个未知编码在 bit 51288 / key `0x4e`。
原 parser 遇未知编码仍前进 4 bit，导致后续边界错位及 bit 51956 的伪时间戳。
同捕获 SE1 和两份 counters-only SE0 的原始 5000-token 前缀均通过。

[tinygrad 的 SQTT parser，固定提交](https://github.com/tinygrad/tinygrad/blob/3b8a7bb8c93e8c7756077dede8829a420bf1b29f/extra/assembly/amd/sqtt.py)
提供了两种额外编码：低 nibble `0xe` 为 `ALU_EXEC`，`0xf` 为 `VMEM_EXEC`，
长度均为 8 bit，时间增量取 bits `[5:4]`。
[AMD PAL GFX12 捕获配置](https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/hw/gfxip/gfx12/gfx12PerfExperiment.cpp)
也区分 ALUEXEC/VMEMEXEC 的排除设置。tinygrad 的实现面向较早架构，不能整体代替 GFX12 表；
本轮只将这两个编码作为候选，保留 AMD 的其他 GFX12 布局，并用整条捕获验证。

隔离补丁在 `source/gfx10/token_types.h` 末尾增加枚举，在
`source/gfx12/gfx12token.cpp` 增加两个 lookup 条目。
现有默认分支消耗事件时间，不为执行事件重复生成已由 `INST` 表示的指令。

独立严格解析完整消耗 56,787,840 字节 / 454,302,720 bits / **43,751,593 tokens**：

- `ALU_EXEC` 11,859,079，`VMEM_EXEC` 1,715,260。
- 未知编码、截断和需另行解释的可变扩展事件均为 0；realtime 单调。
- wave start/end 各 109,072；`NEW_PC_GFX12` 416。

原生 decoder 随后报告 125 个 dispatch（两个驱动内部事件 + 123 个网络事件），
替代旧错位结果的 5312 个。三份对照现有的 **11 个输出文件逐字节不变**。
这解决了 token 层问题，但详细指令 PC 仍为零，不能在此处宣称恢复完成。

### 2. wave 路由与计算寄存器路由不一致

独立跟踪整条流发现，109,072 个 wave 全部使用 `(ME=1, pipe=2)`，
3993 条计算寄存器写入却全部使用 `(ME=0, pipe=0)`。
原 `get_wave_start` 按 wave 路由访问空的程序地址，ISA callback 因而没有被调用。

第二份隔离补丁仅修改 `source/trace_parser.hpp`：GFX12、非 ROCm 格式且原路由地址为空时，
只有存在**唯一已初始化计算路由**才使用其程序地址；存在多个候选时仍保持未解析。
这只是在本次串行、单计算路由捕获中经过交叉验证的诊断兼容处理，
不是通用多队列支持，也未提交到上游或生产工具链。

独立原始寄存器状态为所有 wave start 解析入口；原生 occupancy 的 218,144 条事件配成
109,072 对 wave，与 123 个网络 dispatch 及原有 Vulkan 时间区间一致。

### 3. LLVM 对部分 RDNA4 指令边界解释错误

路由修复后虽然 PC 有效且 decoder 没有 warning，保存的 LLVM 反汇编仍会将
`d72c0003 0200430a` 错拆为 `.long 0xd72c0003` 和一条 `v_cndmask_b32_e32`；
驱动 ISA 中同一组字节实际上是一条 `v_mul_lo_u32 v3, v10, s33`。
因此“机器码字节相同”和“所有 PC 落在某个内核内”仍不足以验证指令边界。

最终 callback 改用先前保存的驱动 ISA，并逐一核对 **23 个 code objects**：
完整 ELF `.text` 哈希、每条指令的全部字节和边界，以及分支地址。
分支目标由编码中的有符号 16-bit 位移计算，避免误解反汇编文本标签。
程序末尾的推测读取也检查了实际填充字节，确认均为 `bf9f0000`（`s_code_end`），
才纳入地址表；没有忽略未知地址或把 `.long` 当成可信指令。

## 验证结果与残余限制

八项人工短 token 样本测试通过，两份隔离 decoder 均构建成功。
最终原生 API 返回 success；119,062 次 ISA callback，无缺失地址、info 或 error。
独立验证器进一步检查硬件记录、指令边界、控制流与 wave 时间范围：

| 项目 | 结果 |
| --- | ---: |
| 详细 wave | 3429 |
| 解码指令记录 | 14,417,516 |
| 入口与独立原始寄存器记录一致 | 3429 / 3429 |
| 以 `s_endpgm` 结束 | 3429 / 3429 |
| 间接转移与硬件 `NEW_PC` 的 wave、起点时间、目标逐一对应 | 416 / 416 |
| 无效指令边界、错误内核/入口、越出 wave 时间范围、完成晚于 wave 结束 | 0 |
| 无法解释的控制流间隙、剩余未匹配入口/间接转移 | 0 |
| 负时长记录 / 对应时间倒序 | 233 / 233 |

控制流验证允许 decoder 对部分等待和控制指令的省略/重建，不跳过 WMMA、算术或内存操作。
最后一项仍是限制：负时长分别出现在 C64 DS（88）、C64 UPS（111）、C128 UPS（32）、
C256 UPS（2）。保存的前十个负时长例子均为 `s_barrier_wait 0xffff`、duration=-1，
符合 decoder 补建 barrier 时序存在边界问题的线索，但未统计全部异常的 opcode，不能断言原因全部相同。
因此这些内核本轮只使用地址/控制流结论，不报告可信逐指令时序或跨内核时长排名。

**C256 DS 的 104 个采样 wave、623,436 条指令没有负时长、时间倒序或其他上述校验异常**。
这是单次插桩捕获的 SE0/WGP0/SIMD0 样本；尚未证明能够代表全 GPU、其他输入或未插桩运行。

## C256 DS 初步热点与研究方向

命中 18,974 个不同指令位置。解码器归属的指令 duration 合计为 **5,591,266 cycles**，
其中 stall 合计 3,658,446；另有单独记录的 idle gaps 3,260,418。
下表分母为跨 wave 累加的 duration，**不包含 idle，不是内核墙钟时间，也不能换算帧时间收益**。

| 类别 | duration 占比 |
| --- | ---: |
| 等待指令 | 43.68% |
| WMMA | 29.37% |
| VALU | 15.64% |
| barrier | 8.03% |
| 其他 | 3.28% |

等待类别可进一步区分：

| opcode | 命中次数 | duration cycles | stall cycles |
| --- | ---: | ---: | ---: |
| `s_wait_loadcnt` | 14,432 | 1,398,568 | 1,384,136 |
| `s_wait_loadcnt_dscnt` | 5048 | 878,386 | 873,338 |
| `s_wait_kmcnt` | 972 | 83,687 | 82,715 |
| `s_wait_dscnt` | 13,460 | 70,800 | 57,340 |
| `s_wait_storecnt_dscnt` | 324 | 10,658 | 10,334 |

load 及 load/LDS 组合等待比单独 LDS 等待突出，支持研究具体加载位置与调度。
等待时间仍可能受依赖链、wave 驻留、同步及其他因素影响；这些数字本身不能证明带宽饱和、
缓存 miss 的因果关系，也不是可直接消除的开销。

按相对内核入口的字节偏移，可优先检查：

- `0x2c4d0`：`s_barrier_wait 0xffff`，84 次命中、282,521 duration cycles，
  占总归属时长 5.05%。邻域包含 LDS fragment 读取和 FP8 转换；仍需严谨对应 GLSL 阶段。
- `0x32160`：另一处 barrier，84 次命中、61,466 cycles，可对照进入同步点之前的工作差异。
- `0x31268` / `0x30208`：与 WMMA 及残差计算交错的 `s_wait_loadcnt 7`，
  可检查能否在保持算术次序的前提下调整独立加载的位置。

下一步先把这些位置映射回源码和寄存器存活区间，结合 EXP-027 的实际 wave 驻留，
选择一个有限的加载调度或存活期干预。候选必须检查机器码、零 scratch、输出一致性和完整宿主重复 A/B；
没有新的因果证据前，不重复公共 DS 代码缩减、扩张线程分组或泛化 LDS 参数扫描。
本轮没有改变算术、精度、归约顺序、权重、尺度或 pass 数，也没有新的画质验收结果。

## 证据身份与复核入口

| 产物 | SHA-256 |
| --- | --- |
| 详细 SE0 原始 SQTT payload | `8cc1b8b1e2876399f56318e482b44b287a4c3638cd3f012a9ceda83d50023cf5` |
| 两种执行 token 补丁 | `6faa0923ba02ad3a99742e4c7e8c8b7d74a54adcfe8022c3354583b9fd57e132` |
| 单计算路由补丁 | `cd5fd4975d5c142aea3a0dd9888d632fda36fb477e6e8ec50b960a52e43da676` |
| 仅 token 兼容 DLL | `b53be53e97a6180c9a4a80cf2b66a3ec5a147748b15f441548ba5399ee76abb7` |
| token + 路由兼容 DLL | `22e5b6be92df5a8ca0f45b69dcdca528e12b7be980a6118ddac3ebf5f560a26f` |

研究目录的 `summary.json` 汇总已完成的交叉检查；`instruction-final-validation.json`
保存最终指令验证结果；`c256-ds-hotspots.json` 保存热点及统计口径。
**可用指令结果仅为 `driver-final-results/timed-hardware-decode/`**。
EXP-027 的旧详细指令结果、EXP-028 `route-results/` 的 LLVM 指令输出不可用于热点；
`driver-results/` 尚缺已验证的末尾填充，也不是最终结果。

脚本及捕获均为忽略的本地研究材料，新克隆不会包含它们。已有材料上的复核顺序为：

1. `audit_prefix.py` / `check_prefix.py`：原表、人工短样本和有界前缀。
2. `probe_exec_tokens.py`、`prepare_decoder.py` / `build-decoder.cmd`：编码候选和隔离构建。
3. `scan_stream.py`、`prepare_route.py` / `build-route.cmd`：完整 token 流扫描及计算路由检查。
4. `driver_isa.py`、`extract_newpc.py`、`verify_instructions.py`：驱动 ISA、硬件间接目标和完整指令验证。
5. `hotspots.py`、`audit_results.py`：C256 DS 统计及证据汇总。

例如最终两项检查使用 `python -X utf8 build/windows/sqtt-compat-study-20261007/verify_instructions.py`
与 `python -X utf8 build/windows/sqtt-compat-study-20261007/audit_results.py`。
脚本默认拒绝覆盖已保存结果；重做时先在独立输出目录准备输入和调整路径，不能直接覆盖本轮证据。
仅复核本轮结论不需要重新运行 GPU。本次提交保存研究摘要及后续使用指引，工具与私有证据保留本地。
没有安装、部署、打包、发布或推送。
