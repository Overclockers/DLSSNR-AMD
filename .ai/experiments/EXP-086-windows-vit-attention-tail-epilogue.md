# EXP-086：ViT attention 完整键块循环与尾块分离

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（correctness_failed）。
运行 `20261008-130553-swin9070xt` 第 10/10 个假设；3 版实现，0 次追加确认。
起始主仓 `88b9385f07d13a6f40c3be39510807a1c016fc26`；宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。

## 假设与实现

当前八次 ViT attention 在新鲜逐层诊断约 0.326 ms。原 K64 循环每轮在完整块/尾块之间分支，LLPC 合流有大量 context 寄存器复制。候选仅在 prenormalized、transposed、split-tail 路线上先按原顺序执行所有完整 K64 块，再执行至多一个原尾块；直接复用未改动的数值 include，保留动态 tokens、K64/K16 顺序、half 分母、FP8 概率、MMA 和其它路径。不同于 EXP-023 预取、EXP-051 宽读取和固定形状特化。候选 C256 单角落另经 CPU 枚举：两个内核各 1596 个窗口仅 8 个单角落，未实施、未另计额度。

4,115 个长度、157,317 个有序块的 (j0,guard) 序列完全相同，覆盖全部尾数以及 0/极小/大长度；两种 BDA 配方的 disabled SPIR-V 精确复现基线。第一版循环后重建尾索引，第二版沿用原循环变量，两者整网 FP32 输出完全相同但均偏离基线：11,470,640 个 float 中 8,602,260 个数值改变，最大绝对差 0.068884969。第一版 ISA VGPR132→114、.text14976→14848 B、静态指令2137→2151，零 LDS/scratch。单 attention 的 finite FP8、mode=4、BDA 探针显示 <=64 keys 一致；65/128/800 keys 分别改变150/218/4498个输出字节，16种长度的输入/保护区均未改写；无尾块的128/256/768/832也不一致，因此不能只归因于尾边界。ISA 中候选把 running den 与 chunk sum 一半先相加，再加另一半，原分支合流保留先求 chunk sum 的路径。第三版对 den 加 precise 约束以阻止重结合，仍未还原基线；目标改变8,602,280个float、最大绝对差0.047883034，探针16种长度均有不同、保护区仍不变。第三版 VGPR83、.text14848 B、指令2166、零 LDS/scratch。源级块序列证明不等于既有机器码浮点行为一致，precise 的传播也改变了其它数值代码生成；不将现有证据夸大为已经完全定位的驱动缺陷。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.840392 / 10.891116 / 10.921970 / 10.937182 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

三个实现均未通过精确正确性，关闭为 correctness_failed。没有完整宿主 A/B、同路径控制或性能确认，不从三帧诊断时间、寄存器下降或代码缩小宣称提速。全部失败源、SPIR-V、ISA、输出和隔离探针保留，生产源码未应用这些改动。

保存 3 份整网 standalone 数值结果，其中 0 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。局部探针结果不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- carried / vitattn SPIR-V：`de60f0c5ea39f10bd83c4e9815f9df81774407f0a7e6db97a619443da508a568`。
- carried / vitattnnobda SPIR-V：`70882c9398227dd7667ded0c79281118677b522f5dad2d14c32ae48853bf0aef`。
- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- disabled / vitattnnobda SPIR-V：`3cdd38d4115abe4c9c063dd519cecd641ee9bc93956745163cf7f8e502d419ff`。
- epilogue / vitattn SPIR-V：`3a9800b4b00f63b560b9f4caebc0d07e112e4b93715ed89861904f3ed73e498a`。
- epilogue / vitattnnobda SPIR-V：`4540ddca65a08db0162b1fbfa36ba27c7c226840fe16ef87f4b76210f3dd7f58`。
- precise / vitattn SPIR-V：`e0e6062c99d7f91eafe4f57027257a1c81d98c9a3b42c98d6038bacf40b46f97`。
- precise / vitattnnobda SPIR-V：`772e017a9f3db9d11b4dc93f4798dc3b8e9e18d7036f1791581f9967827e9222`。
- carried.patch：`3d8a68d5a3b8610f32fad9611e6006a6b25704b05150d3f8f99925b521f3345c`。
- precise.patch：`a1cb913fb438a765db749f9b5ea37fe7e773f974073706ea70950d3a9e37e87d`。
- prototype.patch：`342dbd1ebbe3e730adf5b51369663985c584d6a38e09fdf91cf76cb14d56e8fc`。

- schedule-proof.json：`085d6a85e6359f4aba52f41caf1852e72728efe426ef865f11ed880ad3019568`。
- image-differences.json：`ecf46c03345f67e85327fcac78c3bea91602b94104dddb89a4358b3a2c15220a`。
- attention-probe/probe.cpp：`fc07187745d430d93591eaecbd1715d8f20373819b2e8f84f150db8635d8fee7`。
- attention-probe/probe.exe：`3da559449462d1526d39ddf8e38290a7ae5b6ee57e2f17aaaa320995a68b4c8d`。
- attention-probe/carried-results.json：`966a87a2853aa117f8680a735a6380aadf2d808e2788265459f7309c7064ef8c`。
- attention-probe/precise-results.json：`fced910ae8b317b333a7fdc966d9289829e4feca364d1742d45406914a14b5d6`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
attention_probe.exe <baseline-vitattn.spv> <candidate-vitattn.spv> <private-output-prefix>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮第10项已关闭，按 budget_exhausted 停止。最终核验本轮唯一保留 EXP-076 及两仓提交/产物；新一轮若重访 attention 循环，应先逐操作对照既有机器码的 half 结合顺序和 precise 传播。EXP-080 的统一预热是测量发现，另行记录，不计入优化收益。
