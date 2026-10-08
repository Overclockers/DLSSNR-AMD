# EXP-067：Persistent 窗口坐标的有界整数商

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-115341-shapes9070xt` 第 2/10 个假设；1 版实现，1 次追加确认。
起始主仓 `8ca1b451ddc0433e98771f360c3bf06a34c48de4`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

六个 persistent DS/UPS 每个 item 都用动态 nr_gx 对 nr_w 做除法与余数。候选在 n,d≤65535 且 d>0 时用 FP32 估计整数商、整数乘积修正，并且只在 q*d≤n 且 n-q*d<d 时接受；其它输入或检查失败使用原 uint 除法。这是整数地址计算，不改网络浮点算术、任务序、参数读取、同步、权重与布局。乘积在该范围和商钳制下不溢出，接受条件直接证明 q=floor(n/d)。区别于 EXP-014/043/045 的层查找、跨任务参数缓存和向量读取。

GPU 探针比较 1,114,112 组商和余数：16 个除数各覆盖全部 16 位分子，另有 65,536 组完整 uint32 输入验证通用回退，零差异。六份禁用 SPIR-V 复现基线，六个 driver .text 均增加 256 B，保留原通用路径；寄存器没有改善，全部零 scratch。目标 standalone FP32 和每份宿主 packed 全图精确一致。ADL 0.5 秒采样覆盖初始化、预热与正式测量，非帧同步。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.862125 / 10.870288 / 10.903622 / 10.937338 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-bounded | 10.991101 | 10.996021 | -0.004920 | -0.011394 / 0.004600 / -0.007965 |
| ba | 11.049399 | 11.049681 | -0.000282 | 0.028946 / -0.006968 / -0.022823 |
| confirm-control | 11.113379 | 11.091480 | 0.021899 | 0.020923 / 0.022876 |
| control-bounded | 11.072056 | 11.073212 | -0.001156 | 0.001871 / -0.004184 |

初测平均慢 0.004920 ms，唯一反序确认平均慢 0.000282 ms；组间方向不一致，确认控制的组差约 0.021–0.023 ms。没有可重复、可与控制波动区分的收益，关闭为 unproven，不保留原型。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- bounded / fswinpds64 SPIR-V：`17907b3013dc0ee182ab1ea7b69ff0b01ba981e73ec2f43f274c5aaebc16b750`。
- bounded / fswinpds128 SPIR-V：`596d98724e7c50911e3d00cb8b7f56f7fcf2d6db1b99e475a458fa63be5397d5`。
- bounded / fswinpds256 SPIR-V：`2d60a87e5181d83928e52dfd61940317b9ce82cd57d17384399ca3dda7ae87b0`。
- bounded / fswinpup64 SPIR-V：`d5e61dadbd57c3100bbb8d20c079bf2f138fe75198df1ca6c1c9291fc28f7d98`。
- bounded / fswinpup128 SPIR-V：`9a5196e4163aa297fe564e40080881d73a2a30378cf1bf2bbb5c52a92c6f75f7`。
- bounded / fswinpup256 SPIR-V：`2b4f3e044b73a56406e3c3f49f89ef2f64dc20eb28dbd3c9ddc7140850acb349`。
- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- prototype.patch：`b4cf93d3e6cde8018c5b7f3ba851523a4522717ba867b08f5becf77fa35fb55b`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：将 persistent Swin 的两个相邻 coherent 32 位激活读取合并为同一 8 字节视图，核验实际指令与逐字节输出。
