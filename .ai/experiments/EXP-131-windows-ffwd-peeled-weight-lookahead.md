# EXP-131：FFWD 权重预取的首步消费展开

日期：2026-10-10（Asia/Hong_Kong）。结论：保留。
运行 `20261009-235203-ten9070xt` 第 1/10 个假设；1 版实现、1 次反序确认。
起点主仓 `879291d141c57ba56d9c3436d2012c9ad1545497`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。
宿主提交 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4` 保留本地 customer-mochi-backed；主仓 main-experiment 为本提交并推送。

## 机制与语义

EXP-127 把预取的 Q2 片段放入 K 循环的条件选择，驱动将部分 b128 权重读取拆成 b32，完整路径变慢。
本次针对该已知失败修复：激活前读取四个命名片段，激活后独立执行 Q2 的 K=0，再按原顺序执行 K=1..3。
只在 ffwd3w 配方启用 NR_Q2_PEELED=1；关闭精确复现原 SPIR-V。
每份 acc3 保持 chunk=0..3、K=0..3 的原累加次序；跨独立累加器的执行次序不参与归约。
权重只读且地址仍为原 chunk 的首个 K16，未引入范围外预取；acc2 的 half 激活、FP8 量化、最终 store 均未改。
无新增同步或共享状态；原有第二 token tile 的尾界处理、生命周期和回退保持。
ABI、权重布局、shader markers、host 构建常量和精度不变。

driver .text 31232→31616 B；静态指令行 4185→4161；VGPR 182→189、SGPR23、LDS/scratch0。
buffer_load_b128=72、b32=32 均与基线相同，避免了 EXP-127 的 scalarization；s_wait_loadcnt 88→72。
这些是实际代码变化，不把静态等待条数或寄存器数换算为预计毫秒。

## 完整路径测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，glslang16.5.0、MSVC14.51。
2258×1270 输入、2368×1280 工作尺寸、800 ViT tokens、123 dispatches，100%模型尺度、1pass。
完整宿主 R11G11B10/depth3，静态照片、无运动历史/SR/FG；D3D12 输入复制至输出复制，含 Vulkan 往返。
每个 A/B 批前原版预热4000帧，随后3帧仅检查输出；正式每进程预热100、测量200帧。
ABBA×3与BAAB×3，各紧随同路径控制×2，合计8000正式帧；全部批次保留，未删样。

| 比较 | 基线 ms | 候选/控制 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-peeled | 11.002723 | 10.967100 | 0.035623 | 0.029772 / 0.037112 / 0.039986 |
| ba | 11.057628 | 11.022002 | 0.035626 | 0.033769 / 0.034503 / 0.038606 |
| confirm-control | 11.042226 | 11.043214 | -0.000988 | -0.002001 / 0.000026 |
| control-peeled | 11.004382 | 11.001551 | 0.002832 | 0.011659 / -0.005995 |

六组收益均超过各自控制的最大绝对组差。登记较小批次均值 0.035623 ms（约0.32%），并非保证下界或游戏FPS。
ADL共有614个有效样本，核心时钟198–2786 MHz、
edge 38–58°C、hotspot 39–82°C。
包含初始化与预热，不与正式帧同步；未调频、未依据遥测删样或校正，不假定固定时钟。

## 精确验证

- FP32整网11,470,640个位模式、所有packed性能末帧与独立原版一致。
- 两套配方各15对生命周期回读、6对gradient/随机/HDR/奇数/小尺寸输出精确一致；
  双队列、replay、resize/DRS、延迟回收、取消/销毁、历史排序和pass切换通过。
- 两套配方各36对目标历史帧与90对格式输出一致，覆盖13种底层VkFormat、非零亚像素motion、
  alpha变化、连续/丢弃/reset/乱序/replay。DXGI别名映射未改，未逐别名单测。
- 两套配方各4对边界输出一致：1920×1080、1920×1536、2368×1280、3840×2160；
  上述宿主检查另含2257×1269、129×65、17×19。未扩大声称到原基线不可用的极小路由。
- 无arena复用的目标尺寸中间数组：两套配方各184对数组一致，
  每套均包含全部16个FFWD输出；完整FP32输出也与原版一致。
  初次整文件比较发现末尾dump附带调度队列：dump-all把最后slot延伸到act.bytes。
  按分配器tokens×32限定b070 pool的24,248,320字节后精确一致；43,136个差异均位于随后队列区域，
  没有忽略任何张量字节或采用数值容差。完整dump和边界审计均保留。
- 三套完整shader构建各65模块；宿主和主仓各自只有g_ffwd3w.spv改变，其余模块与markers完全一致。
  因既有两仓配方差异，主仓前后模块在同一已验证宿主资产中替换验证；未声称全主仓模块组合的GPU验收。
- 两仓最终注释完善后，启用/禁用各自重编译：开启与实测模块一致，关闭与各自独立原版一致。
  C++/ABI无变更，复用身份核验的production DLL。未触及Linux、模型或权重，无游戏测试、安装、部署或发布打包。

## 身份与复现

- host-before/g_ffwd3w.spv：`3ad3da9ee684c7951e5d37a7bca62af1ca9e8357b9abbeabbd2cd662aeaaabdb`。
- host-after/g_ffwd3w.spv：`2d3400f917303b26afc3290d6c52aada81fca9c44f6f0665ce60598316f94b5a`。
- main-before/g_ffwd3w.spv：`a59b53e0b34494de718298d4efc9e2c57f3efd01b102648885b7dcfddf446d56`。
- main-after/g_ffwd3w.spv：`38b059235181749fbd83f82ee1077db15ddf9e780253fa83b03cd315599f14ba`。
- dump-tail-audit.json：`9183fc0e1e115dadfc99cb4973427fcf879d7d13d180ae90a4e65a26af57fdfd`。
- intermediate-validation.json：`0391965a7195211000450c02c30dc060235c7281fae80a7e3357615578519694`。
- build-closure.json：`fadedb22755f27e2abe9ac12883e7e6a57f4fd93d7786a5f845336c5ea5104ee`。
- validation.json：`eea2b6080ea39f2cb58309b5720c2f4501fe9c7c971df3436072a91d814061ee`。
- validation-main-after.json：`e96eabd89a16aff0b33da1ebd515ef092863addb4e4a3fdc9c6635d65e301a5f`。
- target-validation-peeled.json：`24aa31829f423450e4485e737b60ed4e2971dd7b9cc7834eb72913029e83a5ee`。
- format-validation-peeled.json：`e9633a50a095a1d082bc84248ac8ca0d7076244280ac4ad366779f35bd36d122`。
- target-validation-main-after.json：`71c5df7925606ad01ca96a9de31db6441f65fd74ef3cae67d1a6cc2977d54e39`。
- format-validation-main-after.json：`57be86e61f993a5e4ebecc86cfb9ae075b12faa70e00b1b1c1b9cc338f6fa9a8`。
- edges-validation-peeled.json：`e3ccb13026dc3bc2f3690ba2e6191ebf9ab7e65abc11345627d3935c84169a7d`。
- edges-validation-main-after.json：`1bdb166e512bbaca218e5154e433c9d57bda1b1a2a68c7e0356609e0773cd6c7`。
- retained-final.patch：`96b7b801e4d8b9470523d14253ebb3a3fcd5fe01142d4c738fa9857cf26df3f1`。
- telemetry-summary.json：`253eb1e7a9d34ae1b4dd2c6bb19a9cb8519270cd790709862f97a3acd2a8ca51`。

```text
python -X utf8 windows/build/build_network.py rdna4 --out <shaders> --glslang <pinned-glslang>
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
nr_benchmark 2258 1270 <plan> --no-reuse --model-pack <model> --spv-dir <shaders> --dump-all <private-dir> --out-image <rgba> --in-image <photo> --warmup 1 --repeats 2
target_history_v2.exe <production-dll> <assets>
target_formats.exe <production-dll> <assets>
```

模型、源码快照、回读和原始计时留忽略目录，本机定位见local ledger。
下一项检查ViT尾块对相同clamped V片段的重复读取。十次完成、保存并核验推送后执行用户指定关机。
