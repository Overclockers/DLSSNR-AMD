# EXP-013：persistent 工作组内任务编号使用共享内存广播

日期：2026-10-07。结论：**保留复用 LDS 的版本；完整离线路径初测省 0.035528 ms，反序确认省 0.033269 ms。**
运行 `20261006-235052-9070xt` 第 6/10 个假设；两个实现变体，一次反序确认。

## 机制与边界

当前六个 persistent dispatch 合计约 3.8 ms。原工作组用一个专有全局 slot 传递领取的任务编号：
thread 0 做 atomicExchange，各 wave 做 atomicAdd(0)。其它工作组和宿主不消费这个 slot。
改为 LDS 内的 uint 广播，保留两道工作组屏障、buffer memory barrier、全部跨组队列原子操作及任务依赖。
每个网络运算、精度、量化和归约顺序不变。共享 slot 的读取先于第二道屏障，随后才可覆盖为交换数据；
上一任务末尾屏障保证其交换读写先于下一次 slot 写入。数组容量、16-byte fragment 对齐及 host packing 均不变。

第一版另加四字节共享空间明显退步。保留版让 slot 与此时尚未使用的交换数组别名，不增加 LDS 容量。
仅用于现有 head-split、单 item 的 persistent DS/UPS 默认布局；普通 persistent 与其它诊断布局继续全局 slot。
没有删减跨组同步、arena 可见性或 host 预留 slots；与 EXP-005 的 inter-dispatch/arena 假设不同。

## 身份和测量

- 主仓基线 `9140872df6c5ca6e5e8ec3ce7cb325599868e376`，宿主基线 `9ece4e464fcfca126d1f47201e2353e8417c2e84`；包含 EXP-012。
- 宿主提交 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`；主仓实现及记录由本实验提交保存。
- RX 9070 XT / 26.8.1 (LLPC)，MSVC 14.51、固定 glslang 16.5.0；2258×1270、工作尺寸 2368×1280。
- R11G11B10 线性照片，scale 1、单 NR pass、默认控制、三帧在途；不启用 history、游戏、SR、FG。
- 完整 D3D12 输入复制→输出复制 GPU 区间，每批 100 帧预热、200 帧采样；频率/温度及持续负载未采样。
- 最终 shader 源 SHA-256：`2c3d24882a0d4b17ccf06946d5bdd0cdae052585cb5410af03877ce8c45bec06`。
- 主仓补丁 SHA-256：`d1c8f8ac5b21dc327fc9cdf563ccc465186a44cac133b2c05b6465f32d310d54`。
- shader 刷新 manifest SHA-256：`1856cd298e3035bdbf621046d6845415092ef0f308a27c3212d1f882a7073ad0`。

| 批次 | 基线 ms | 候选 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| 新增独立 slot，ABBA×3 | 10.775607 | 11.354915 | -0.579308 | -0.592224 / -0.569819 / -0.575883 |
| 复用 LDS，ABBA×3 | 10.802575 | 10.767047 | 0.035528 | 0.028187 / 0.037852 / 0.040545 |
| 同路径 AAAA×2 | 10.808993 | 10.808585 | 0.000408 | -0.005771 / 0.006588 |
| 复用 LDS，BAAB×3 | 10.873444 | 10.840175 | 0.033269 | 0.033844 / 0.024694 / 0.041269 |

保留版本初测约 0.329%，反序确认约 0.306%；六组均改善。控制最大绝对组差约 0.0066 ms，
确认最小组收益约 0.0247 ms；控制不作为普适误差界。没有筛选最快批次或叠加跨会话时间。
共 44 批普通测量 / 8,800 帧，加两次 12 帧 smoke；冷启动 smoke 不作为性能结论。

## 正确性与构建闭包

- 46 份完整照片输出与独立保存基线一致。宿主 production DLL 无变化，配候选 shaders 的 15 份序列、
  五对目标/奇数尺寸梯度、照片、随机棋盘和 HDR 图像全部一致；normal 多遍切换通过。
  序列覆盖 replay、双队列、尺寸保留、延迟收集、历史顺序、格式、DRS、取消及尾部销毁。
- 宿主六个原 shader 重建逐字节复现。清理后的六个候选与实测版本逐字节一致；完整生成 64 模块，
  其余 58 个不变。不需要修改/重编 C++；沿用 EXP-012 已验证的 production DLL。
- 主仓与宿主既有 unroller 和 edge-body recipe 不同，完整产物不能跨仓强求同一 hash。
  主仓独立保存原 recipe 基线并重建前后 64 模块，确认也只变六个；将受影响六个模块分别置于
  相同已验证宿主链路，额外比较两组各 15 份序列及照片/随机/HDR 三对目标图像，全部逐位一致。
  这验证主仓配方下的受影响内核，不声称完整主仓原生包或游戏路线已验收。
- 第一版编译遇到 explicit shared block 与普通 shared 变量混用，修复布局后继续；
  清理时限制到已测 DS/UPS，保持三个普通 persistent 模块不变。准备脚本重复上下文断言未改动生产源。

```powershell
python -X utf8 <matched-build_network.py> rdna4 --glslang <pinned-glslang> --out <candidate-shaders>
<profile_host.exe> <matched-DLL> <private-assets> <csv> 3 0 200 100 2258 1270 <output.raw> <photo.rgba>
```

源快照、两个 recipe 基线、编译参数/哈希、全部 CSV/输出和验证报告保存在忽略目录，ledger 记录位置。
未改 Linux、权重、模型尺度或 passes；未进入游戏、打包、部署、发布或推送。
