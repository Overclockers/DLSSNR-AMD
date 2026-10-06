# EXP-011：合成直接读取原始输入

日期：2026-10-07（Asia/Hong_Kong）。结论：**保留无损优化；完整 NR 离线路径初测节省 0.048548 ms，反序确认节省 0.037930 ms。**
无人值守运行 `20261006-235052-9070xt` 第 4/10 个假设，一个实现变体，使用一次追加确认。

## 机制与适用条件

EXP-007 直接采样宿主输入后，encode 仍写一个 RGBA32F 原色副本，transfer 再读取它。
在现有 sampled-input 条件内，原始输入纹理直到输出 blit 才被覆盖；因此让 transfer 直接采样它，
并用新增 `runtime_encode_direct.spv` 省去 keep 的 imageStore。FP32 proxy 和合成公式完全不改。
目标分辨率每帧省去约 45.9 MB 的原色副本写入，并以 packed 输入读取替代该副本读取。
为原路径回退保留 keep 分配，**不是 VRAM 容量优化**。

条件仍为线性 R11G11B10/RGBA16F、scale 1、单遍容量、无预处理/外部 mask，并要求非 native-compose。
录制持有的 `LinearInputBinding` 同时持有 encode 与 transfer descriptor sets；原图 view 继续随录制保留到 consumer tail 完成。
它只引用已有原图，不按裸句柄缓存，不增加旧网络/分配的保活链。创建额外 set 失败返回空 binding，走原路径。
保留旧 encode variants；其它配置继续原代码。所有 RGB 运算、alpha、精度、归约、模型设置和输出转换不变。

## 身份、环境和构建

- 主仓基线 `c566699afa886c40df2844e4cbe55e984a6ef248`，宿主基线 `dfd564e1d888aa35f6effec8b11867452310dae8`，已包含 EXP-009。
- 宿主已提交 `e32e312856c56d6b3275f6907f3055fb3a6aa649`，主仓实现、工具和记录由本实验提交保存。
- RX 9070 XT / 本次 Vulkan 查询 `AMD proprietary driver / 26.8.1 (LLPC)`；WMI 查询被拒绝。
- 2258×1270 输入、2368×1280 工作尺寸，R11G11B10_FLOAT 线性照片，100% 模型尺度、单遍、默认控制、三帧在途。
- 性能不启用预处理、运动/history、游戏、FSR4 或 FG；未持续采集 GPU 负载、频率、温度。启动阶段只见约 1.2% WebView 3D。
- 固定 MSVC 14.51、glslang 16.5.0；候选完整 Windows DLL、主仓 C++17 核心、保留的主仓离线工具均构建通过。
- 从最终生成入口重建 63 个模块：62 个旧模块逐字节不变，新增 1 个 direct encode；无模型复制/分发。
- 新 shader SHA-256：`e50dfc1314fe0add510ea214a7beaf397ddb83a05ac926a3bd884565e7016891`。
- 核心/生成入口补丁 SHA-256：`e93e0990e3ccfcb30d1c7f111763b65d28f93268179acf97ba736331470a92a4`。
- 最终无诊断构建身份 SHA-256：`8ec9ed8262ccf8ce0e2adba6811cc0bbdd850dcf71a5e66bd84f4a736ce5e969`。

## 完整路径测量

使用同一离线 harness，原/候选独立匹配 DLL 与资产，100 帧预热、200 帧/进程。
普通验收只记录完整 D3D12 输入复制→输出复制 GPU 区间；分段诊断另外执行。

| 批次 | A ms | B ms | 节省 ms | 组差 ms |
| --- | ---: | ---: | ---: | --- |
| A/B/B/A×3 | 10.932859 | 10.884310 | 0.048548 | 0.059151 / 0.048731 / 0.037762 |
| B/A/A/B×3 确认 | 10.907394 | 10.869464 | 0.037930 | 0.046294 / 0.037019 / 0.030478 |

初测和确认分别约 0.444% / 0.348%。六组均改善，没有筛选最快批次。
同路径 A/A/A/A×2 的组差为 0.020195 / 0.012953 ms，平均 0.016574 ms。
初测对照存在偏移，因此追加反序确认；确认最小组收益约 0.0305 ms，仍大于本次所见控制组差。控制不是通用误差界。
全部普通/确认/控制批次的单帧标准差范围 0.044794–0.057990 ms。

两次独立分段筛选：encode **0.067262→0.044112 ms**，compose **0.192956→0.178581 ms**，
core writeback 0.040790→0.032416 ms；其它段也有漂移，不把这些差值相加宣称整网收益。
合计 34 次性能程序调用、6,800 个采样帧、3,400 个预热帧；其中 2 次详细诊断。

## 正确性与工具

- 34 份性能研究完整照片输出逐字节匹配独立保存基线。
- 新 [check_direct_original.cpp](../../windows/build/check_direct_original.cpp) 比较 FP32 proxy 与 composition。
  257×129、R11G11B10/RGBA16F、四个白点、五组合成控制：**48 份数组、6,365,376 个 32-bit 值全部相同**。
  包含全部有限 half 编码、负 half、subnormal 和 HDR；不宣称 NaN/Inf payload 或其它驱动等价。
- 原保留构建与此候选的 15 份现有宿主序列输出一致，覆盖 replay、双队列、尺寸保留、延迟收集、
  历史顺序/重置、packed/sRGB 格式、DRS、取消和 completed-tail 销毁。
- 五对独立完整图像一致：目标梯度、2257×1269 梯度、目标随机棋盘、HDR 有限值图、照片。
  基线直接复用 EXP-009 相同 production DLL/fixture/驱动及已保存输出，其身份重新核验；未重复跑未变基线。
- 候选 ABI、normal 多遍切换、同进程原路径/直接路径切换通过；额外 set 创建失败由空 binding 源级回退保障，未注入真实驱动 OOM。
- 最终主仓 `profile_host.py --production` 完整构建通过；无私有诊断导出，ABI 和新增 FP32 probe 再次通过。
  构建期间核验全部宿主源/工具身份，全部生成 shader 与已测试集合逐字节一致。

```powershell
python -X utf8 windows/build/profile_host.py --host <host-checkout> --out build/windows/host-study
<profile_host.exe> <matched-DLL> <private-assets> <csv> 3 0 200 100 2258 1270 <output.raw> <photo.rgba>
<check_direct_original.exe> <shader-directory>/runtime
```

详细参数及 probe 用法见 [宿主离线文档](../../docs/windows-host-profiling.md)。
原始源快照、匹配编译参数/哈希、CSV、输出、验证报告与重建 manifest 位于忽略研究目录，本地 ledger 保存其位置。
两个严格定位的准备脚本断言曾中断文件改写（重复文本/相邻 diff 锚点）；修复后构建与验证推进，没有增加实现变体或改变产品语义。
未改 Linux、宿主安装/打包入口，无游戏/发布验收、打包、部署或推送。恢复发布时须检查两个新增 encode shader 的完整分发/卸载闭包。
这里的约 0.04 ms 是相对 EXP-009 的离线增益，不能与不同时段历史绝对耗时相加承诺游戏帧率。
