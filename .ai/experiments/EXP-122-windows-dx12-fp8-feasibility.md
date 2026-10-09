# EXP-122：DX12 FP8 GEMM 与 C256 DS 可行性尝试

日期：2026-10-09。状态：**可行性门槛检查完成：当前驱动不支持 SM 6.10 / LinAlg；没有性能结论**。

已按用户授权临时开启 Windows 开发者模式并完成验证。预览 runtime 加载成功，
但当前 26.8.1 驱动最高报告 SM 6.9、LinAlg Tier 0，原生 FP8 shader 的 PSO 创建被拒绝。
实验结束后已恢复开发者模式原设置。本轮未进入生产 GEMM/C256 DS 移植或有效性能 A/B。

## 目标与边界

用户确认按“代表性 FP8 GEMM → C256 下采样热点内核”的顺序尝试 DX12。
这是新的定向可行性研究，不恢复已耗尽的 EXP-117–121 优化额度。
核心 checkout 为 `main-experiment`，起点 `4500c2be9c1a941108c96e060946c3dc60184f7a`。
默认精度、权重、归约及已有生产实现不变。宿主只读，未开始游戏测量。

本次选择标准 HLSL LinAlg 预览接口。结论仅覆盖这条路线，未评估 AMD 私有扩展等其他 DX12 实现。
第一阶段必须先确认原生 FP8 运算可执行，再移植生产 GEMM 的分块与 LDS 调度，随后才推进 C256 DS。

## 已完成

- 获取微软官方便携包：Agility SDK `1.721.3-preview`、DXC `1.10.2605.37-preview`。
  `D3D12Core.dll` 与 `dxc.exe` 的 Authenticode 均验证为有效 Microsoft Corporation 签名。
  没有安装 SDK 或驱动；预览 runtime 由独立 executable 的导出选择。
  开发者模式按用户授权临时开启，保存原值并在试验后恢复。
- 编译能力探针，查询 wave32、FP8 E4M3FN 输入、FP32 累加及 16×16×16 矩阵形状。
  准备了系统 runtime 对照和能捕获新建探针进程加载器信息的小工具。
- 编译 `gemm.hlsl`，DXIL 确认包含 FP8 矩阵 load 与 `linAlgMatrixMultiplyAccumulate`，
  不是标量矩阵乘法替代。其输入 A 为 `[M][K]`，权重 B 为 `[N][K]`，与项目方向一致。
- 编译独立 DX12 执行、回读、GPU 时间戳和参考校验程序；普通标量 shader 仅用于验证框架。
- 使用项目已有 pinned glslang 和 `nrvk.hpp` 构建相同运算的 Vulkan FP8 对照。

设备：RX 9070 XT；本次 Vulkan 查询 `AMD proprietary driver / 26.8.1 (LLPC)`。
DX12 与 Vulkan 的 adapter LUID 一致。输入为合成的带符号四分之一整数，精确参考由整数乘加得到。
本阶段无图像分辨率、无模型、无历史、无 SR/FG。

| 检查 | 结果 |
| --- | --- |
| DX12 标量控制，M=64、N=128、K=256 | 8192/8192 输出逐位一致，尾部 guard 未改写 |
| 同一控制开启 D3D12 调试层 | 0 个错误；3 个缓冲初始状态被按 COMMON 处理的提示 |
| Vulkan 原生 FP8，同形状、同输入 | 8192/8192 输出逐位一致，尾部 guard 未改写 |
| 预览 runtime 与实验 shader 开关 | 开启开发者模式后均成功；确认加载本地 1.721.3-preview |
| 当前驱动能力 | 最高 SM 6.9，LinAlg Tier 0；FP8/FP16 wave MMA flags=0，shape_count=0 |
| DX12 原生 FP8 shader | 编译通过；PSO 创建返回 E_INVALIDARG，调试层明确拒绝 SM 6.10 |
| DX12/Vulkan 有效性能 A/B | 未完成 |
| C256 DS 移植 | 尚未开始，依赖原生 FP8 执行门槛 |

控制运行只有少量预热与样本，用于验证框架，不报告提速百分比。
标量控制与 Vulkan WMMA 的耗时不能比较为 API 性能差异。
合成数据能检查布局与运算，但不替代全范围 FP8、逐层或整网数值验收。

## 实际门槛与交叉验证

起初 `AllowDevelopmentWithoutDevLicense` 未设置。预览 runtime 的实验功能启用和设备创建均返回
`0x887e0003 / D3D12_ERROR_INVALID_REDIST`；沙箱内外结果相同。
捕获到 Windows 加载器的明确诊断：

> Preview releases of D3D12Core require Developer Mode.

系统自带 DX12 runtime 能正常创建设备并执行控制 shader，但不识别本次 LinAlg 预览查询。
这一步只能定位系统前置条件，不能据此断言驱动不支持；初始报告保留为
`capability_determined=false` 和 `native_amd_fp8_16x16x16=null`。

用户随后明确回复“开启”。保存原设置后，通过 Windows UAC 仅设置该 DWORD 为 1。
重新运行得到：

- `D3D12EnableExperimentalFeatures` 与 `D3D12CreateDevice` 均为 S_OK。
- 模块路径确认为实验目录的预览 `D3D12Core.dll`，没有误用 System32 的旧 runtime。
- 查询 SM 6.10 返回 S_OK，但 `HighestShaderModel=0x69`（SM 6.9）。
- LinAlg tier 查询返回 S_OK / 0；wave size 0 和 32 的 FP8、FP16 查询均返回 S_OK / flags=0 / shape_count=0。
- 沙箱外再次查询结果相同；三个枚举出的 AMD adapter 条目结果一致，正式控制使用的 LUID 与 Vulkan 相同。
- 实际创建 SM 6.10 FP8 PSO 返回 `0x80070057`。D3D12 调试层错误 881 列出最高
  `cs_6_9`，并报告当前 shader 版本无法识别。失败发生在 dispatch 之前。

因此最终能力结论为 **当前已安装驱动未暴露此标准 DX12 LinAlg 路径**，
不是“DX12 更慢”，也不是“9070 XT 硬件不支持 FP8”。
最终报告为 `capability_determined=true`、`native_amd_fp8_16x16x16=false`。

实验后通过同一受限脚本恢复原设置：原来 key 存在、value 不存在；现在 key 仍存在、value 已移除。
恢复日志及独立注册表读取均验证成功。没有更换驱动、修改其他注册表值或 Windows Update 策略。

## 继续条件与证据

AMD 官方 `26.10.07.02` AgilitySDK Developer Preview 驱动说明明确列出 RX 9000 的
SM 6.10 / Linear Algebra Matrix 支持。本次没有安装该驱动；是否更换系统驱动是新的操作范围。
后续如使用具有所需能力的驱动，先重新开启开发者模式并运行能力探针，
通过后执行原生 FP8 小矩阵的完整回读和调试层检查，扩展输入与矩阵形状。
生产性能比较前需对齐时间戳和 barrier 口径：当前 DX12 框架的区间含最后一个 UAV barrier，
Vulkan helper 只在重复 dispatch 之间插入 barrier。随后移植真正的 GEMM 分块和 C256 DS，
维持原舍入、归约、边缘和同步语义，以重复交错测量验收。

私有源码、工具、SDK、DXIL/SPIR-V、编译命令、下载哈希和日志在忽略的
`build/windows/dx12-probe-20261009/`，入口说明为其中的 `README.md`，汇总为 `summary.json`。
本机路径见忽略的 `.ai/local/dx12-probe-20261009.md`。
未形成生产优化，未修改宿主文件、部署、打包、发布或推送。保留原先已暂存的 `.codegraph/.gitignore`。

参考：[微软 LinAlg 预览](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/)、
[Agility SDK 版本](https://devblogs.microsoft.com/directx/directx12agility/)、
[AMD 26.10.07.02 官方支持说明](https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-MS-AGILITY-SDK-26-10-07-02.html)。
