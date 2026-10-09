# EXP-130：Persistent 末层消费者读取剔除

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-fresh-five9070xt` 第 5/5 个假设；1 版实现，1 次追加确认。
起始主仓 `b237df35b842432ec3deaf8494d16a01e542828b`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

host 将消费者表初始化为 0xFFFFFFFF，并且仅把 rec[k] 的依赖加入 rec[k-1]（1<=k<n）。因此 rec[n-1] 的所有槽保持 sentinel，与分辨率和窗口数量无关。候选仅在 nr_layer+1<n_layers 时执行消费者表读取/发布块，涉及六个 DS/UPS shader；原 memoryBarrierBuffer、工作组 barrier、非末层原子/队列、完成计数和 epoch 重置均保持。区别于 EXP-046 入队合并、EXP-077 屏障合并和 EXP-085 原子读取语义。

六份 disabled SPIR-V 精确恢复基线，候选整网 FP32 11,470,640 位模式及每批 packed 输出一致。六个 VGPR/LDS/scratch 均不变；DS64/128 的 SGPR 各+1，UPS64/128 各+2，两个 C256 不变。每份 ISA 多一条 s_cmp_lt_u32，DS64 已检查该条件与 lane<4 的执行掩码合并，在消费者 buffer_load_b32 前跳过；原 barrier 保留。C256 DS .text 224512→225024 B。terminal-invariant.json 和 resource-comparison.json 保存源码不变量与逐模块资源身份；没有新硬件事务计数或完整队列覆盖结论。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.009683 / 10.973646 / 10.992009 / 10.992915 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-terminal | 11.015318 | 11.009723 | 0.005595 | 0.006783 / -0.001084 / 0.011086 |
| ba | 11.037035 | 11.045058 | -0.008023 | -0.012158 / -0.010745 / -0.001166 |
| confirm-control | 11.039691 | 11.028592 | 0.011099 | 0.025489 / -0.003291 |
| control-terminal | 11.006293 | 11.015758 | -0.009465 | -0.013881 / -0.005049 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

unproven，不保留。初测 11.015318→11.009723 ms，平均省 0.005595 ms，三组 +0.006783/-0.001084/+0.011086 ms；初测控制组差 -0.013881/-0.005049 ms。唯一反序确认 11.037035→11.045058 ms，三组全部更慢，平均慢 0.008023 ms；确认控制 +0.025489/-0.003291 ms。初测收益没有复现，不追加测量。

ADL 合计 661 个有效样本，核心时钟 125–2709 MHz，edge 41–58°C，hotspot 41–82°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V：`3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad`。
- disabled / fswinpup64 SPIR-V：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- terminal / fswinpds64 SPIR-V：`afbd36401af5cb2fc99ad17d20dd77f0fd8cc224dd22a201061ede500d4a3e84`。
- terminal / fswinpds128 SPIR-V：`5d993772ff33d2cc91788ff7d09243663b69432ef0892b14051dbee7fb840747`。
- terminal / fswinpds256 SPIR-V：`9c51b3a77215ab8a4f7d3bf4c27b2d4ea015561c011fabe63f813f9ddd832f0b`。
- terminal / fswinpup64 SPIR-V：`a57f6c29eedf42251ce613863716bc2114511dcb1d04c7534e9ca023cbdcee16`。
- terminal / fswinpup128 SPIR-V：`3a86b9254b052fedc96a08f7645924bbb3c17c8feaf21e0bdae7e7cc1926400a`。
- terminal / fswinpup256 SPIR-V：`21732e7cb6ae258e8b9f66706e2d80c2a93919091cd245b100fc2f6b8ddd144c`。
- prototype.patch：`58bb5afd18e072cee25bb063c7d1736aa28c2197a278e2d6207630154e860c0b`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮 5/5 预算耗尽，完成提交/推送核验后结束；后续新研究应基于当前真实指令与完整路径成本选择机制，不继续扫描这些小幅源码调度变体。
