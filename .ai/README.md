# Fork 工作记忆

本目录服务于跨聊天接续、优化实验和宿主交付跟踪。它不是新的实现规范，也不保存聊天流水账。

## 阅读顺序与职责

1. [context.md](context.md)：目标、用户确认的运行链路和事实边界。
2. [status.md](status.md)：当前工作、验证状态、下一步和阻塞。
3. [decisions.md](decisions.md)：长期决策、理由和可选有损优化的要求。
4. [experiments/README.md](experiments/README.md)：实验索引、记录格式与结果。

后续研究建议见 [EXP-025 之后的研究方向](research-directions-20261007.md)（2026-10-07）：
旧证据复盘和待验证候选，不是新的实验结果。
随后完成的 [EXP-026 瓶颈实测](experiments/EXP-026-windows-swin-bottleneck-profiling.md)
确认 C256 DS 指令缓存低命中信号，并降低了纯队列领取优化的优先级。
继续研究的 [EXP-027](experiments/EXP-027-windows-c256-common-code.md) 将机器码缩小 12.8%，
但完整宿主收益未确认；补齐了实际 wave 驻留证据，详细 SQTT 指令解码仍有兼容边界。
随后完成的 [EXP-028 SQTT 兼容定位](experiments/EXP-028-windows-sqtt-compatibility.md) 修复了隔离工具中的
执行 token、计算路由和 ISA 指令边界问题，旧详细捕获的地址/控制流校验通过。
C256 DS 的 104 个采样 wave 时序无异常，加载等待值得下一轮研究；尚无新的生产提速结论。

实现约束以 [CLAUDE.md](../CLAUDE.md) 为准；贡献约定见 [AGENTS.md](../AGENTS.md)。

新 Windows shader 调度/代码生成实验使用 [分阶段实验工具](../docs/windows-experiment-workflow.md)：
缓存构建，先精确输出与 driver ISA 机制检查，再启动性能初筛；按阶段保存壁钟耗时。
保留前仍做无缓存重建及完整验收，不改变假设预算、精度要求或测量协议。
稳定架构见 [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)，画质验证遵循
[NGX-VERIFICATION.md](../docs/ngx-verification/NGX-VERIFICATION.md)。引用这些资料，不复制维护。

## 更新与保存

- 后续所有实验修改和记录使用 `main-experiment`，编辑、提交及恢复旧运行时核对分支；不直接改本仓库 `main`。
- 目标改变、实验结束、出现阻塞、完成集成或准备交接时更新 `status.md`，控制在约 100 行以内。
- 实验结束后同步索引和状态；失败结果也保留，注明不适用范围及重新测试条件。
- 过时细节移入相关实验记录；长期决策注明生效日期和替代关系，不通过删除失败记录美化历史。
- 公共目标、计划、决策和精简实验摘要进入 Git。正文使用中文，文件名、标识和命令使用英文。
- `local/` 被 Git 忽略，只放本机路径、私有笔记及本地定位记录。新克隆按需创建。
- 原始日志、性能采集、编译结果、对照输出放现有忽略的 `build/` 或 `artifacts/`。
  权重和模型继续遵守项目分发禁令；本目录不保存模型、DLL 或采集到的张量。
- 公共记录不得依赖本机绝对路径；记录可复现命令、输入类型、配置和产物标识，本机位置另记。

## 跨仓库边界

本仓库维护 NR 网络核心。外部 `dlss-5-amd-project` 维护 Mochizuki 宿主接入、菜单与打包。
本项目涉及宿主的修改统一使用 `customer-mochi-backed` 分支；编辑或提交前核对当前分支，不直接改宿主 `main`。
这里记录宿主版本、所需接口、整合结果和游戏验证状态；宿主实现细节在宿主仓库维护。
离线基准通过、宿主构建通过、游戏验证通过是三个独立结论，不能互相代替。
