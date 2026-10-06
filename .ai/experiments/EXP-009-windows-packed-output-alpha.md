# EXP-009：省去 R11G11B10 输出的重复 alpha 回写

日期：2026-10-07（Asia/Hong_Kong）。结论：**保留无损优化，完整离线 NR 区间平均节省 0.113991 ms / 1.032%。**
无人值守运行 `20261006-235052-9070xt` 第 2/10 个假设，一个实现变体，无追加确认测量。

## 机制和实现范围

R11G11B10 输出没有 alpha，也不使用 `runtime_alpha` 的 8-bit RGB 舍入。该 pass 仍逐像素读取并原值写回
FP32 RGB，只改变随后格式转换会丢弃的 alpha；目标分辨率下形成一次多余的整图 dispatch/读写。
仅在 `VK_FORMAT_B10G11R11_UFLOAT_PACK32` 下跳过这个 dispatch，继续执行原有合成、同步、布局转换与输出 blit。
其它格式沿用原条件。不改 RGB 运算、权重、精度、归约、模型尺度、遍数或资源寿命；没有新 shader 或有损选项。
主仓和宿主各只改 `windows/src/core/nr_runtime.cpp` 的一个条件及解释注释。

## 身份和环境

- 主仓基线 `ac790668e528bf4ab73faec99f1eec95f580f7f8`；宿主基线 `e20ab4997d680b454ce6dd5dc6574561926c2736`。
- 已同步宿主提交 `dfd564e1d888aa35f6effec8b11867452310dae8`；主仓实现和记录由本次实验提交保存。
- 当前会话 Vulkan 查询 RX 9070 XT / `AMD proprietary driver / 26.8.1 (LLPC)`；WMI 被拒绝。
- 输入 2258×1270，工作尺寸 2368×1280，R11G11B10_FLOAT，线性，100% 模型尺度，单遍，默认控制，三帧在途。
- EXP-007 直接输入编码开启；性能用既有验证照片，无历史/运动、预处理、SR、FG 或游戏。
- 原/candidate DLL 均从保存的原始宿主 core 和固定 MSVC 14.51 环境独立编译；C++20 `/O2 /MT`。
  候选另外生成无诊断生产模式 DLL；主仓对应源码 C++17 单独编译通过。
- 62 个 SPIR-V 模块全部复用已核验原字节；原和候选共用同一私有资产目录，未重新量化或生成模型。
- 运行前未发现游戏/其它基准；此前三次 GPU Engine 采样见 WebView 3D 约 1.15–1.20%，未持续采样负载、频率或温度。
- 候选补丁 SHA-256：`c706a2294aea9168d77a314f1deffd8857268881bbdf36b9a3cf0c373cee2471`。
- 无诊断候选 DLL SHA-256：`703b48b5a07291e463e2cdbeaeade50c6b7e48cdb16905050f7a8121513307d1`。
- 对应宿主 core SHA-256：`188eb8b83e50fccad978565e48d89f00bd788cfe6d6debaf4d67b57dd7d86034`。

## 性能方法和结果

[宿主离线工具](../../docs/windows-host-profiling.md) 采用同一个计时程序，在原/candidate DLL 之间交错。
每个进程 100 帧预热、200 帧采样，A/B/B/A×3；测量 D3D12 输入复制到输出复制的完整 NR GPU 区间，
不启用额外分段 timestamp。另做 A/A/A/A×2 的独立同路径控制，不把两个 timestamp 域直接相减。

| 组 | 原路径 ms | 候选 ms | 节省 ms |
| --- | ---: | ---: | ---: |
| 1 | 10.995183 | 10.894077 | 0.101105 |
| 2 | 11.051428 | 10.933820 | 0.117608 |
| 3 | 11.090760 | 10.967501 | 0.123259 |
| 平均 | 11.045790 | 10.931800 | 0.113991 |

三组都改善约 0.101–0.123 ms。独立同路径控制组差 0.006230 / -0.015125 ms，平均 -0.004447 ms。
控制不是普适误差界；此实验收益大于所见同路径差异且方向一致，没有选择有利子集。
普通 A/B 每批 200 帧标准差 0.049155–0.061897 ms。

独立两次分段筛选中，core writeback 为 **0.175053 → 0.040975 ms**，compose 基本不变；
完整诊断区间为 10.923777 → 10.843977 ms，operators 同时有漂移。最终收益采用上表普通交错测量，
不拿该局部减少替代完整路径结果。性能研究共 22 次调用、4,400 采样帧、2,200 预热帧。

## 正确性和适用边界

- 22 份性能研究照片输出逐字节匹配原保存基线。RGB 仅省去 identity 存取，R11G11B10 无可观察 alpha。
- 原/candidate 无诊断 DLL 各通过 ABI 与完整现有宿主 GPU 夹具；额外完整捕获 15 份序列，逐字节一致。
  包括 replay、两队列、保留 geometry、延迟收集、历史顺序/重置、packed/sRGB 格式、DRS、取消和 completed-tail 销毁。
- 无诊断构建另外比较五对完整输出：2258×1270 梯度、2257×1269 梯度、目标随机棋盘、HDR 有限值图、目标照片，全部字节一致。
  HDR fixture 包含 0、2^-24、2^-16、sRGB 阈值、普通亮度和最高 65024 的数值；不宣称覆盖 NaN/Inf payload。
- 原/candidate 均通过 normal 多遍切换，包括容量复用和独立新会话输出对照；未改变容量管理，不重复预算/OOM 注入测试。
- 无诊断 DLL 不含私有 profile 导出；主仓 C++17 编译通过。完整 shaders 无变化，未引入新 manifest/ABI。
- 序列输出 SHA-256：`9a372028da3913a1de69c4b4b20b751c51f77aed225f42bc58293ba0250bf85c`。

## 复现和保存

```powershell
python -X utf8 windows/build/profile_host.py --host <host-checkout> --out build/windows/host-study
<profile_host.exe> <original-or-candidate-DLL> <private-assets> <csv> 3 0 200 100 2258 1270 <output.raw> <photo.rgba>
```

分别保存原/candidate core，使用相同宿主源码、编译参数和 shader。生产验证构建去掉 `NR_DIAGNOSTICS`；
现有 `host_contract.exe` 按文档执行 ABI/GPU，另用 `--startup <output> <width> <height>` 和 `--pass-switch normal`。
所有命令、源/产物身份、补丁、输出、CSV、逐批分布和完整验证日志留在忽略研究目录；本地 ledger 保存其位置。
未修改 Linux 或宿主构建/安装入口，未测游戏/FSR4/FG、目标尺寸运动序列或发布 CI；没有打包、部署或推送。
本地优化和离线约 1.03% 收益不代表游戏 FPS 承诺。
