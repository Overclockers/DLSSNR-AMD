# EXP-155：ViT 概率 pair 在完整块内立即消费

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261010-224000-ten9070xt` 第 10/10 个假设；1 版实现，0 次追加确认。
起始主仓 `0bb73ea1f28902e76e1125abe06bc51d94bebb50`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

仅原 NR_VKPERM 完整块路径保留前两个概率 pair，在 c4/c6 各完成一 pair 时计算原 pair0+pair2、pair1+pair3 并更新各自原 partial 链。各 kb 次序、half 操数和舍入、ph、最终分母与 context 均保留；尾块和其它模式保持原路径。不同于 EXP-102 的 C32 跨 key-tile 调度、EXP-107 携带类型、EXP-116 logits 分批、EXP-138 转换和 EXP-144 跨 lane 完成归约。

关闭宏的 SPIR-V 精确复现基线；实际候选全网 FP32 逐字节一致。首次构建因源码展开后死分支数组索引被编译器检查而失败；用 (c>>1)&1 保持所有活分支映射且让死分支也合法，属例行构建修复，不增加实现变体。失败构建的 inherited shader 未执行 GPU 或用于候选验收。LLPC .text14976 B 不变，静态指令2137→2138，仅多一条 s_delay_alu；VGPR132/SGPR33、零 LDS/scratch 不变。追踪 QK accumulator→half pack→指数整数夹位→概率生成→partial packed add，在排除 s_delay_alu/s_wait_alu 后的零基结构位置：基线 c4/c6 生成650/653、消费655/656；候选生成651/653、消费655/656（寄存器重分配已跟踪）。这些是指令位置，不是周期。机制证据 SHA256 `abc32d5cded0d0035c1039b33ba0909d65393d9b03b86c71a8503a5f4b579069`。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.963855 / 10.966668 / 10.968056 / 10.955780 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

两个完成的概率 pair 在基线和候选中仍都先于任一对应 partial 更新；候选没有实现 c4 更新先于 c6 概率生成。局部 c4 间距5→4、c6间距3不变，不等于源码计划的立即消费/消除跨 c4 更新的第二 pair 生存区间，也没有降低整体寄存器或代码尺寸。按 mechanism_not_realized 提前关闭，不进入完整宿主性能计时；没有声明提速或退化。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
在 driver 机制门槛提前关闭；没有运行本项 packed 宿主对照、性能 A/B 或追加确认，也未扩大到 non-BDA、其它格式、边界/尾部尺寸、运动历史、中间值、replay、fallback、两仓完整集成及无缓存重建。静态 FP32 一致不能替代这些验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- immediate-fixed / vitattn SPIR-V：`ac3d59ed04303f1066710ecde15f2430b1b62c6db3bc72e4afe19b0a711696b7`。
- prototype.patch：`74a6287aaf5106ada857f4cc5f0e143bbc67847b45731c4e84ef50a1535eff8a`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮十次额度耗尽；核验所有记录、提交、核心远端、资产和预先暂存内容，保存终态并停止，不自动启动新实验。
