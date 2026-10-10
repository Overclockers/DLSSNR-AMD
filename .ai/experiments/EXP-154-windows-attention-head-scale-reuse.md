# EXP-154：C512 每 head 缩放的只读加载复用

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261010-224000-ten9070xt` 第 9/10 个假设；1 版实现，0 次追加确认。
起始主仓 `6527a2b3721df45f9d9f2231272606a0fe222cb7`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

原型在 head 循环入口读取同一个 pc.s_off+h 标量并按原 NR_F16 窄化，五个 Q/K epilogue include 点复用该值。原 QK 归约、normalization、乘法表达式与顺序不变；不同于 EXP-081 residual 广播和 EXP-139 GEMM channel gain 复用。模型权重只读，head 地址有效；关闭宏恢复原表达式。

disabled SPIR-V 精确复现基线，目标 standalone FP32 与全部完整宿主 packed 回读一致。LLPC 标量权重 descriptor s[20:23] 不变，实际 s_buffer_load_b32 4→1、s_cvt_f16_f32 4→1，标量等待 s_wait_kmcnt 13→3；这些是静态代码位置计数，不是动态次数或收益预测。VGPR184/SGPR60、LDS9216 B、零 scratch 不变；.text26880 B 不变，静态指令3882→3866。例行准备脚本曾误判 include 数量（实际五个）及漏导入 difflib，均在成功构建/GPU 执行前修复；没有额外实现变体或无效候选测量。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.963855 / 10.966668 / 10.968056 / 10.955780 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-reuse | 10.978124 | 10.991531 | -0.013407 | -0.011324 / -0.019438 / -0.009459 |
| control-reuse | 10.993497 | 10.996533 | -0.003037 | -0.006405 / 0.000331 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

机制实现，但完整路径 10.978124→10.991531 ms，平均慢 0.013407 ms；三组均变慢（0.011324/0.019438/0.009459 ms），同路径控制偏差为 -0.006405/+0.000331 ms，候选退化在三组中均大于控制绝对偏差。因此按 regressed 关闭，不追加确认、不保留实现；指令数量减少不等于端到端提速。

ADL 合计 300 个有效样本，核心时钟 58–2764 MHz，edge 37–55°C，hotspot 38–80°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / attn SPIR-V：`4bafe70c5fa393acc1b0d6c1f53715a48f1cc6b9adf137d42d23caabdf6c62dc`。
- reuse / attn SPIR-V：`5c9a7cbdbd157c042cd105e6578903144487d1c3ae0d6481faf773a1ab970179`。
- prototype.patch：`6e373a541936693335fe9a56784107903a4b95237eb5de4bd90ee8c2e7f04d51`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：完成最后第十个已授权假设：ViT key-permuted 完整块的概率 pair 在 c4/c6 立即消费，保持各 partial 链顺序与尾部路径。
