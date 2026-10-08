# EXP-051–065 的结论与下一轮研究入口

日期：2026-10-08（Asia/Hong_Kong）。本页是已结束的 10+5 项实验复盘和只读源码检查，
没有新优化实现、编译或 GPU 测量，不占新一轮额度。建议不是提速结论，具体证据以各 EXP 为准。

## 已确认的结果

| 运行 | 已启动/关闭/上限 | 保留项 | 完整离线 NR 节省 |
| --- | --- | --- | --- |
| `20261007-214238-codegen9070xt` | 10/10/10 | [EXP-056](experiments/EXP-056-windows-vit-contract-fixed-dimensions.md) | 反序确认 0.037738 ms |
| `20261008-003019-five9070xt` | 5/5/5 | [EXP-061](experiments/EXP-061-windows-gemm-remap-pair-division.md) | 初测 0.014475 ms，反序确认 0.021672 ms |

两轮均为 exhausted / budget_exhausted，不可用 resume 再开额度。EXP-061 已继承 EXP-056，
未做两项合并对最初版本的直接配对测量，不把各批收益相加称为实测总收益。
EXP-061 采用较小批次均值汇总；确认中一组处于控制波动内，不是保证下界。
环境为 RX 9070 XT、26.8.1 (LLPC)、2258×1270 输入、2368×1280 工作尺寸、800 tokens、
123 dispatches、100% 模型尺度、1 pass，完整宿主采用线性 R11G11B10、depth=3。
这些是离线 NR 时间，不是游戏帧率，本轮也没有建立 Windows/Linux 性能比。

## 能指导选题的结论

1. **图不变量和整数地址计算值得优先检查。** EXP-056 将收缩 K=4096、N=1024 暴露为编译常量，
   M 保持动态；EXP-061 将 G=2 映射中 gt=1/2 的通用除法化简，所有工作组坐标和顺序不变。
   两项保持矩阵运算、精度和归约，支持继续审计已知形状、步长和动态除法。
2. **静态指标只能解释机制。** EXP-056 的 VGPR 70→95 仍提速；EXP-061 保留通用回退，
   静态代码增大，但实际 G=2 路径受益。反过来，EXP-051 少 perm、EXP-062 少两个 gain load、
   EXP-065 少四个格式转换，都未形成稳定完整路径收益。不能单看寄存器、代码大小或 load 数量。
3. **Linux 分块依赖在此 Windows ViT 对上没有迁移收益。**
   [EXP-052](experiments/EXP-052-windows-vit-tile-dependency.md) 的工作组/wave 发布版，
   反序确认局部算子对分别慢 0.022377/0.019085 ms；
   [EXP-053](experiments/EXP-053-windows-vit-token-chunk-locality.md) 的 512-token 分块仍慢 0.029177 ms。
   这是具体实现的局部结果，不能乘以层数当整网变化，也不证明所有跨 dispatch 优化无效。
4. **CPU/API 调用优化的优先级要服从完整区间证据。**
   [EXP-064](experiments/EXP-064-windows-compatible-buffer-binding.md) 确实省掉 34/123 次绑定，
   初测/确认仅省 0.001673/0.010050 ms，组间方向不稳。当前配置下没有采用依据；
   这不等于 CPU 录制耗时为零，或其他宿主负载永远无收益。
5. **数值语义和实际选中路径要先过关。**
   [EXP-063](experiments/EXP-063-windows-quant-half-integer-range.md) 最初只改 mode 4，
   gemmvact/gemmprojws 实际用 mode 1；SPIR-V 改了而 driver .text 没改，相关计时不是候选证据。
   接到真实路径后平均慢 0.023119 ms。
   [EXP-065](experiments/EXP-065-windows-cached-noise-half-transfer.md) 直接传 half 漏掉 signaling NaN
   的 quiet bit；修正后穷举一致，性能仍未证实。half 优化先查全部 65,536 编码；
   图像输入优化还必须覆盖宿主实际选中的 runtime_preencode。

## 成本分布与选题顺序

重新汇总五轮起点 `ranking.log` 的 123 条逐层记录，按日志的一位小数微秒求和。
这是 **standalone FP32 诊断，已含 EXP-056、尚未含 EXP-061**；不是新采集，
也不是完整宿主计时。用于选题，下一轮需重新建立当前基线。

| 分组 | 诊断时间 ms | 占算子合计 |
| --- | ---: | ---: |
| 六个 C64/C128/C256 persistent DS/UPS | 3.7979 | 36.98% |
| C32 pre/post、六个普通层、DS/UPS | 3.4940 | 34.02% |
| 全部八组 ViT | 1.5422 | 15.02% |
| C512 与 decoder 等其他算子 | 1.4353 | 13.98% |
| 合计 | 10.2694 | 100% |

日志 SHA-256：`0b6a6b313ee2bb5e0005adf97ea3869879edb65357c84df33e06c03e9d6951f5`。
Swin/C32 合计约 71%；后续研究不应全部局限在 ViT。

**先查更多可证明的编译不变量。** 当前只有 gemmprojws 开启 NR_FIXED_KN，宏硬编码收缩尺寸。
候选是 gemmvact 的 K=1024/N=4096，以及 gemmvqkvnorm 的 K=1024/N=3072；后者选择点已有形状检查。
先核对所有可达调用和两仓差异，再设计按模块的常量参数；不能给扩张直接打开现有收缩宏。
保留动态 M、原 K 累加顺序、尺寸和路线回退。映射中剩余 lin/(2*gy) 也可审计，但必须先证明
实际 gy 的主机网格契约。同一底层机制在不同模块或参数上的变化，不能为填额度任意拆分假设。

**再对高成本阶段做有针对性的定位。** 先核对当前 shader 与旧捕获身份，重用
[EXP-028](experiments/EXP-028-windows-sqtt-compatibility.md) 最终验证的 C256 DS 样本。
加载等待支持追踪具体生产者、消费者和存活区间，不支持预测可省多少帧时间。
EXP-029 bias 预取数值失败、EXP-030 contract 权重预取未证实、EXP-043 参数外提和
EXP-045 参数向量化退步；重访须解释新机制如何解决已知失败。
image pre/post 的主体也值得定位；噪声的四条转换只是一小段。EXP-038 单 wave、
EXP-042 整数采样、EXP-047 post/compose 融合已有失败或未证实记录，避免原样重试。

**把测量改善作为研究工具工作。** 现有 A/B 与控制有组间波动，ADL 每 0.5 秒的数据包含初始化/预热。
下一轮可先对齐正式帧与遥测，评估统一预热和控制稳定性，保留全部批次；
不把控制差当固定误差界，也不靠追加次数挑小收益。EXP-032 双上下文测量尚未证明能降低噪声。
只研究观测方法、不测试优化时属于 discovery，不消耗优化假设。

## 暂降优先级与复用资产

- 无新证据时暂缓 EXP-052/053 分块依赖/提交、EXP-058 buffer-reference 地址路径、
  EXP-060 相邻 K16 布局，以及 EXP-063/065 的量化/噪声替换。
- EXP-059 的完整 subgroup 契约对所测八个 GEMM 的完整 driver ELF 逐字节相同；
  当前驱动和这些模块无需再做同一 flag 的性能批次。
- 保留全部负面结果和控制。unproven 是未建立可重复收益，不是证明绝对零收益。
- 可复用实际中间 FP8 算子对夹具、half 穷举探针、边界/回退检查，以及 EXP-056/061 的
  126 对目标历史夹具（13 种底层 VkFormat、reset/discard/replay）。历史输入需预生成、
  序列后写盘，避免跨过生产的 250 ms 历史连续性门限。
- 本机位置见 `.ai/local/nr-optimize-handoff-20261008.md`。旧脚本有固定 ledger、路径和额度，
  使用前核验、适配，不直接执行旧 charge/finish/shutdown 脚本。

## 新会话入口

先读 `.ai/README.md`、`status.md`、本页、索引及相关 EXP。宿主 checkout 必须为
**customer-mochi-backed**，当前为 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`，包含两项保留改动。
新 `$nr-optimize N` 建立独立运行，先核验源/产物、driver 和新鲜基线；不 resume 两轮 exhausted 状态。
编号以当时索引的下一个空闲号为准。继续严格无损、Windows-only、离线，不修改宿主 main。
上轮一次性关机动作已发出、返回码 0；它不是后续每轮的自动结束动作。

可用的开场内容（5 是示例额度，用户可替换）：

```text
$nr-optimize 5
这是新一轮。先读 .ai/status.md 和 .ai/research-directions-20261008.md，
继承 EXP-056/061 的保留基线，按当前热点与已有失败证据自主选题。
宿主只使用 customer-mochi-backed，严格无损、离线实验。
```
