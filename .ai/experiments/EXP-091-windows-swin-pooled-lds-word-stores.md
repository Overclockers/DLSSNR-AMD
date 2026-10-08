# EXP-091：Persistent DS 池化 LDS 的成字写入

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-162203-lossless9070xt` 第 5/10 个假设；1 版实现，1 次追加确认。
起始主仓 `d46a1e04d083197d620c73ea473502c33607b189`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

C64/C128/C256 DS 的每个有效lane保留原half池化与FP8转换，把已有两个4字节向量按原位打包，通过原lds_x的uvec2别名一次写8字节。地址、mask、LDS容量、同步与消费者不变。896个lane写入区间覆盖各自全部16*C字节，8字节对齐且不重叠；别名只用于已有x-first persistent DS布局。

65,536组独立shared-memory探针覆盖全部256种FP8位模式，含signed zero/NaN，原与packed回读且输入位模式都精确；pool-layout-proof.json SHA256 `2b1f4297b7f4df19148234ccd1869b25a5f02839701d2536158d71e621dbd68c`。关闭三个SPIR-V精确还原；目标FP32和packed全图精确。实际并非ds_store_b64：驱动以ds_store_2addr_b32替代每8个ds_store_b8，DS64/128为64→0个b8加8个双地址store，DS256为192→0加24个。代码各减512/512/1280B，但增加16/16/48个v_perm_b32，VGPR与LDS容量不变、零scratch。探针补齐int8算术扩展后构建通过。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.019102 / 11.015484 / 11.018076 / 11.025782 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-packed | 11.042365 | 11.033382 | 0.008983 | 0.013845 / 0.008596 / 0.004509 |
| ba | 11.075496 | 11.080595 | -0.005099 | -0.002995 / -0.005509 / -0.006792 |
| confirm-control | 11.073035 | 11.078564 | -0.005530 | -0.010094 / -0.000966 |
| control-packed | 11.058313 | 11.058006 | 0.000307 | 0.000833 / -0.000219 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测11.042365→11.033382 ms，省0.008983 ms；唯一反序确认11.075496→11.080595 ms，反而慢0.005099 ms，确认三组均反向。反序控制均差-0.005530 ms，包含-0.010094 ms组差，收益不可与控制波动可靠区分。判为unproven并回退，不追加测量或扩大验收。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V：`3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad`。
- packed / fswinpds64 SPIR-V：`7957fdbdc714c4be1fa70d0572f16507b0af069c2fc1f1065bbc24b87ace0a95`。
- packed / fswinpds128 SPIR-V：`32af582b11ec9e24d4c1c9bb745905b5ed774e9fb1ea3447693fc2349c02c91c`。
- packed / fswinpds256 SPIR-V：`be17a96102fa59dd90035f8b9570eaa68cd629e11531a5f83e0436dc4cae7d46`。
- prototype.patch：`7fd1d0f3a4bc176046fd0dfa3c4c37c7d1236a0c6e4ae78f82309aa63da6fdf8`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：转向完整合成路径中signed cbrt的符号处理，保留原pow及特殊值回退。
