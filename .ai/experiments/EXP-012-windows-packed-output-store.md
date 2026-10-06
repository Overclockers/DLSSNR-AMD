# EXP-012：合成直接写入 packed RGB 输出

日期：2026-10-07。结论：**保留无损优化；完整离线路径初测省 0.075179 ms，最终无诊断构建确认省 0.081756 ms。**
运行 `20261006-235052-9070xt` 第 5/10 个假设，一个实现变体、一次反序确认。

## 机制与范围

EXP-011 后仍先写 FP32 合成输出，再 blit 成 R11G11B10。新增 `runtime_transfer_packed.spv`，
只改变最终 imageStore 的目标格式；合成公式、FP32 运算、模型、归约和输出精度不变。
每个 invocation 读取原图的自身 texel，然后将合成结果写回该 texel，省去 FP32 合成写回和后续 blit。
原 surf0 与 keep 分配继续用于模型及 fallback，不宣称减少相应 VRAM 容量。

沿用直接输入路径限制：线性输入、scale 1、单遍容量、无预处理/外部 mask、非 native-compose；
进一步限于 R11G11B10，且宿主必须实际启用 `shaderStorageImageExtendedFormats`、设备支持该格式 storage、
输入图像具备 storage usage。核心 `HostDevice` 新标志默认 false；其它接入不自动启用。
宿主记录图像实际 usage，binding 持有额外 descriptor set；分配失败返回空 binding。
进入/离开直接写回时纳入 compute write 访问和阶段，保持录制完成前的图像/view 寿命。
不满足条件时使用原路径。RGBA16F 运行时输出仍使用旧转换。

## 身份与环境

- 主仓基线 `309cdf3820c9e547eb5f8786ee16d5259c517ba1`；宿主基线 `e32e312856c56d6b3275f6907f3055fb3a6aa649`，包含 EXP-009/011。
- 配套宿主提交 `9ece4e464fcfca126d1f47201e2353e8417c2e84`；主仓实现、探针、文档和记录由本实验提交保存。
- RX 9070 XT / Vulkan 报告 `AMD proprietary driver / 26.8.1 (LLPC)`。MSVC 14.51、固定 glslang 16.5.0。
- 2258×1270、工作尺寸 2368×1280，R11G11B10 线性照片、100% 模型尺度、单 NR pass、三帧在途。
- 性能不启用 history、预处理、游戏、SR 或 FG；未采集时钟/温度或持续后台负载。
- 原始构建参数、源快照、DLL、输入/输出、日志及哈希保存在忽略研究目录；ledger 保存其路径。
- 新 shader SHA-256：`7f25cb1fac16344f1a49d170d51f24297d4c041009400339ed9f61fa888022e0`。
- 核心/生成入口补丁 SHA-256：`c14f8ce26ec6cce83050389e86d7da293e75e3626dd7f3673a453137418cffe2`。
- 最终离线构建 manifest SHA-256：`933a26895cada16c2b0e7f58e0aec65f972873eddbad66128f03d1b00326a240`。

## 完整路径性能

完整 D3D12 输入复制→Vulkan NR→D3D12 输出复制区间。每进程预热 100 帧、采样 200 帧；
初测使用诊断 DLL 的非分段模式，确认使用最终无诊断 DLL，两组各自在相同模式内比较。

| 批次 | 基线 ms | 候选 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ABBA×3 | 10.836010 | 10.760831 | 0.075179 | 0.063503 / 0.087852 / 0.074182 |
| BAAB×3，最终 production | 10.862683 | 10.780927 | 0.081756 | 0.082495 / 0.081728 / 0.081046 |
| AAAA×2，同路径 | 10.854835 | 10.866785 | -0.011950 | -0.016659 / -0.007240 |

初测约 0.694%，最终确认约 0.753%；六组均改善。控制差不是通用误差界，未挑最快帧。
普通批次单帧标准差范围 0.044536–0.059331 ms。
验收共 32 批、6,400 个测量帧；另有 10 帧 smoke 和两组各 100 帧的分段诊断。
分段诊断 compose 0.177464→0.132564 ms，core writeback 0.033418→0.000292 ms；
其余段也有波动，不将分段差值相加、不混用不同会话的绝对值计算累计或游戏收益。

## 正确性与构建

- 独立转换探针：513×257、131,841 像素；所有有限 R11/R10 相邻值的 midpoint ±2 FP32 ULP、
  可表示端点、随机有限正负值/subnormal/HDR，blit 与 storage 输出逐位一致。
- 实际 composition：257×129、两种输入格式、四个白点、五组控制，共 40 份数组 / 1,326,120 个 packed 像素一致。
  R11G11B10 覆盖实际原地读写；RGBA16F 只做独立目标的算术探针，并不扩展运行时适用范围。
- 35 份计时/smoke/诊断完整照片输出与独立基线一致；候选 15 份宿主序列及五对额外图像一致。
  序列覆盖重放、双队列、尺寸保留、延迟收集、历史顺序、格式、DRS、取消及已完成尾部销毁；
  额外图像为目标梯度、2257×1269 梯度、随机棋盘、有限 HDR、照片。复用已核验 EXP-009 保存输出，EXP-011 也已与之匹配。
- ABI、normal 多遍切换、同进程直接/原路径切换通过。私有故障注入构建分别关闭实际设备功能与图像 storage usage，
  两条回退输出均与基线一致；没有把这些测试环境变量写入产品。未注入真实驱动 descriptor OOM。
- 主仓 C++17 核心、宿主 production DLL、最终 `profile_host.py --production` 完整构建通过；
  64 个 runtime/network 模块匹配已测集合，原 63 个模块字节不变。最终 ABI 和探针再次通过，无诊断导出。
- 不宣称 NaN/Inf payload、其它 GPU/驱动或未测游戏画面的等价。

```powershell
python -X utf8 windows/build/profile_host.py --host <host-checkout> --out build/windows/host-study --production
<check_packed_output.exe> <shader-directory>/runtime <check_packed_output.spv>
<profile_host.exe> <matched-DLL> <private-assets> <csv> 3 0 200 100 2258 1270 <output.raw> <photo.rgba>
```

探针已纳入本仓离线工具，见 [用法](../../docs/windows-host-profiling.md)。纯转换测试 shader 不属于产品资产。
恢复发布时须核验新增 runtime 模块的完整打包/卸载闭包；本轮没有修改发布入口、打包、部署、游戏测试或推送。
两个私有原型文件曾先于账本登记生成；在任何编译/GPU 执行前补记为第 5 次，保留原始哈希与顺序偏差说明，没有重置计数。
