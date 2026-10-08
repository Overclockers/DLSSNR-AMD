# EXP-102：C32 概率生成时的部分和调度

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（no_codegen_change）。
运行 `20261008-193306-lossless9070xt` 第 6/10 个假设；1 版实现，0 次追加确认。
起始主仓 `159441cf8e145c6d01c6114d3fdc43195f261d5c`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

C32 token-split 路径原先生成全部 half 指数，再求分母部分和。原型在每个 key tile 的指数生成后立即更新对应 query 的四个 half-pair 部分和。每条链仍从 half 零开始，按 j=0/1/2/3 加法，最后保持 ((part0+part1)+part2)+part3、XOR16 交换、half 两分量相加、倒数和量化。矩阵 K/query 访问顺序和 LDS 流量不变；与 EXP-003 的 query-first logits 和 EXP-037 的 context 流式消费不同。只对 fswin32 开启。

disabled SPIR-V 精确复现基线；候选 SPIR-V 不同，但驱动 .text 38144 B 逐字节完全一致，4867 条指令、VGPR189、SGPR33、LDS2048 B 和零 scratch 全部不变。数值顺序证明 SHA256 `33146e25ba9f2f70883fd206385b8b0ae98198d87fce0723aa76a5e4453056f0`。目标 standalone FP32 以及已完成宿主 packed 回读精确一致。筛选脚本原先在 ISA 后自动继续计时，看到机器码一致后中断了本实验自己的测量进程；已完成五个 200 帧批次和三个短跑，原始文件保留，未完成的 ab-producer-1-1 不计入正确性或性能结论。筛选脚本现已在驱动代码不变时停止，避免重复计时。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.999239 / 11.010161 / 11.007372 / 11.000571 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

编译器已把原来的部分和安排为相同机器指令，本原型没有改变目标执行代码，no_codegen_change，不保留。不从未完成的 A/B 批次提取收益，不追加变体或确认。

ADL 合计 155 个有效样本，核心时钟 101–2765 MHz，edge 40–55°C，hotspot 41–79°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 8 份宿主回读、1009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswin32 SPIR-V：`572651948aaf9c0daf2a2ea2f0a3669065b4a6824d24c7d0827cbe290f8a426a`。
- producer / fswin32 SPIR-V：`0da9f264a90f85e13e4e14518c7df5d5f9583d21a3ce8333eae0a8a4b827432e`。
- prototype.patch：`0646a1bfcb79faf559204e8db5ea8c3c4d8c2a668ecf7aeb76047177685e510d`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续剩余四次额度，检查 decoder nearest upsample 的四个输出像素是否可以共享同一源投影读取。
