# 参考项目审阅：RDNA4 HIP porting

日期：2026-10-06。类型：源码及既有实验记录审阅，**本轮没有运行 GPU 基准或形成新提速结果**。

## 身份与比较边界

- 参考仓库：`lmxxf/dlss5-on-amd-9070xt-porting`，本地 HEAD
  `7574e0de8053ee4baa1bc944329660d81bbfdf8b`，提交时间 2026-10-04。
- 当前网络：`DLSSNR-AMD` 的 `a6829dd`；宿主：`dlss-5-amd-project` 的 `38ea72c`。
- 宿主已接入独立的 lmxxf HIP 后端。其 `third_party/lmxxf/UPSTREAM.md` 记录已完成
  0.41 pin `b687e13a8fcb8efd5be905ebbd0c9d70e15d88e3`；功能及本地验证见
  `docs/lmxxf-041-consumer-review.md`。本地参考克隆没有该对象，未独立核验祖先关系。
  不应把参考克隆的旧模板或整个后端重复导入当前宿主。
- 参考克隆三份模板都设置 `NETWORK_FREE_RES=0`、`NETWORK_HEIGHT=auto`、`FIT_LARGE=1`、
  `FAST_NUMERIC=1`、空 `SKIP_BLOCKS` 和单遍；普通游戏模板另开 `VIT_ADAPTIVE=1`。
  `src/native_network_geometry.h` 的默认 tier 路径会将超出上限的大输入拟合到 1080 档；
  自由分辨率是另一个可选路径。不能将这组默认成绩视作 2258×1270 原尺寸完整 NR。
  宿主自己的 lmxxf 默认自由分辨率开启，与参考克隆模板不同。
- 对方“逐位一致”通常对其上一版基线，不能推出与当前 Mochizuki 一致。
  `hip/README.md` 也记载 float-FMA 数值基线变更。当前默认精度和开关要求继续遵循
  [decisions.md](decisions.md)，不采用另一项目的默认有损策略。

## 可以借鉴的工作

| 优先级 | 思路 | 参考证据与当前适用范围 |
| --- | --- | --- |
| 1 | 离线完整 NR 调用链分段计时 | `Development/HIP/experiments/outside-net/rt_outside.cpp` 区分 CPU 录制、D3D 输入/输出、网络与中间间隔，并比较逐帧 CPU 等待和三帧在途。应移植测量方法到本产品当前录制 ABI；不能直接复用旧 ABI1 调用顺序。 |
| 2 | 合并输入格式转换与线性颜色编码 | 对方 DIRECT_IO 让生产者直接写共享输入，去掉私有缓冲到共享缓冲的额外复制。当前 Mochizuki 输入是纹理到共享缓冲、再到 VkImage，结构不同；可先研究核心内部格式转换后又编码的两步，避免额外整图中转。未测收益。 |
| 3 | 输出合成直接抵达最终传输布局 | 借鉴“消费者所需布局由生产者直接生成”。需保留 R11G11B10 等格式的舍入、颜色处理、alpha、DRS、历史和录制寿命。当前存在输出图像回写及 image→buffer，但这些步骤不能无条件删除。未设计或验证原型。 |
| 后续可选 | 自适应复用 ViT 结果 | 参考 `Development/HIP/VIT-REUSE.md` 和 `hip/README.md`。属于跨帧近似；运动、遮挡、切镜、控制参数变化需要独立验证。若实施，独立菜单及持久 INI、默认关闭，关闭恢复全算。当前离线静态基线不足以验收。 |

对方 `Development/results/zero-copy-io-20260928/README.md` 的输入直写只省约
0.02～0.05 ms（其 add-on 回放）；`outside-net-20261002/README.md` 的 ABI 输入直写
在其 1080 流水线测试约省 0.07～0.08 ms。它们说明要测完整路径，不能当作本项目收益预估。
后者文档中的 GPU 段差值、帧间隔和不同批次值也不能混作同一个计时量。

## 当前宿主的具体切入点

宿主 `OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/backend/mochizuki_runtime/` 下：

- `MochizukiNrRuntime.cpp:3999` 的 `CoreRecordInputs`：D3D12 颜色和可选运动纹理复制到共享缓冲。
- 同文件 `:3424` 的 upload、`:3454` 的颜色上传：`vkCmdCopyBufferToImage`，DRS 时另填充边缘。
- `:3531` / `:3536`：调用 `Runtime::record_engine` / `record`。
- `:3566`：核心输出图像复制到共享缓冲；`:4060` 的 `CoreRecordOutputs` 再复制到供 SR 使用的纹理。
- `:3575` / `:3587` / `:3591`：生产者 Signal、Vulkan 提交、消费者 Wait。
  正常单队列路径已经是一次跨队列往返；额外队列切换还涉及现有资源寿命和排序保证。

**计时口径澄清**：宿主取 `runtime->last_gpu_ms()`；vendored
`windows/src/core/nr_runtime.cpp:1376` 到 `:2041` 的时间戳包住 `record_all`。
因此屏幕上的 network GPU 数字还包含核心内部输入处理、线性编码、适用的历史处理、
输出合成和回写，不是仅矩阵算子之和；外层 buffer↔image 搬运、D3D12 复制、
跨队列等待、SR/FG 不在这个范围。此前“不能用截图整帧减网络准确归因”的结论继续有效。

线性输入下，核心先将颜色图转换/复制进 `tex_in`，再由 `runtime_encode.comp`
读写它并保存原颜色。第一个具体候选可让 encode 从原格式图像取值，直接生成相同的
proxy 和 keep，保持色彩公式及舍入；先限于目标格式/单遍/100%尺度，再验证其它入口或回退。
更激进的直接读共享缓冲还要处理 row pitch、格式解包与采样语义，后置。

计时工具应对查询槽和提交编号做明确关联，GPU 完成后再回收；分别记录 CPU 时间和 GPU 区间。
对方 `net-timing-20261002` 记录过 HIP event 的错误短读数和修复成本，
`outside-net-20261002` 也记载时钟校准偏移。应借鉴其诊断经验，不能直接照搬原始时间戳相减。
分段探针与低插桩总耗时分别采集，并用同批、同负载的往返次序验证候选。

## 已有对应实现或暂不迁移的方向

| 参考思路 | 本项目对照与判断 |
| --- | --- |
| C512 mix→expand→activation→contract 寄存器融合 | `windows/shaders/rdna4/ffwd3_t.comp` 已有权重作为 A、激活作为 B、累加器直接接下一级输入的实现，无中间 LDS。参考项目 `mochizuki-gap-20261001` 也明确从该组织借鉴。EXP-006 试的是继续跨到后续 projection，两者范围不同，不能把参考项目 FFN 成功当成推翻 EXP-006 的证据。 |
| 成组 FP8 转换/打包 | 宿主 `windows/build/quad_quant_glsl.py` 已把适配循环改为四元素转换，`include/coopmm.glsl` 有对应 helper。对方 pack8 的约 9% 是其旧 HIP 基线的收益，不能叠加到当前路径。仍可审计遗漏的生产调用点，但 EXP-003/004 已测的变体及融合舍入失败不盲重扫。 |
| 单 wave C32、窗口融合、持久化 | 当前 `fswin32` 已配置 `NR_FWAVES=1`、`NR_HWAVES=0`，C64/C128/C256 有持久化系列。结构优化需指出实际新增差异，不能仅照搬名称。EXP-001/002 已保留的调度也继续有效。 |
| 精简同步、GPU 自旋等待 | 对方 `handoff-gpu-20260930` 中探针收益没有变成整帧收益；当前 EXP-005 也未建立同步/绑定收益。保留必要依赖，只有新测量发现具体冗余才重新做。 |
| LLVM23、HIP `hipStreamQuery`、手写 AMD 指令 | 属于 HIP 编译器/提交路径。当前网络由 glslang→SPIR-V→Windows LLPC 编译，不能把升级 HIP 编译器当成当前 Vulkan 优化。两条后端各自验证。 |
| fast numerics、跳块、缩模型、多遍预测 | 改变数值、结构或实际处理量。多遍预测对当前单遍目标无收益；其余只作独立默认关闭的有损功能评估，不能冒充默认无损提速。 |

参考项目的 `same-queue-20261001` 把官方 Mochizuki 的 vkd3d 同队列接入作为对照。
本用户宿主是原生 D3D12＋共享缓冲 Vulkan，不能套用该官方接入的成本判断；
也不能把它在一台机器上的跨队列数值当成所有 Vulkan 集成的固定下限。

## 后续与完成边界

下一轮先做优先级 1，再按分段数据决定优先级 2/3 的最小原型。
继续 2258×1270、单遍、100%模型尺寸和原有精度；游戏测试仍按用户要求暂缓。
本次仅更新工作记忆；三个仓库的生产代码未修改，未编译、未运行 GPU、未打包、未提交或推送。
机器路径及完整源文件哈希另存忽略的本机记录。
