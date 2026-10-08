# EXP-089：C32 Q/K 归一化共享 rsqrt

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-162203-lossless9070xt` 第 3/10 个假设；1 版实现，0 次追加确认。
起始主仓 `f5f3471599e7d5de557859ee1a75b6e869b18b50`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

三个普通C32模块由低半wave计算Q的rsqrt，高半wave计算K，交换已窄化的结果并选回nq/nk；保持max、f32 rsqrt、half转换、原求和树和后续量化。与EXP-033交换norm输入不同，本项删除重复SFU计算。

65,536种half编码包含非有限值，通过真实half加法/限幅/rsqrt验证partner因子逐位一致；norm-proof.json SHA256 `a502d03273d2852cd273d932d1c8a45b45bef1b060e3962e97b85e69d7f994d0`。关闭三模块SPIR-V精确还原。每模块v_rsq_f32从8减至4，但permlanex16从8增至12，选择指令与代码大小增加；无scratch。探针一次保留字标识符编译错误已修正，非实现变体。

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
| ab-shared | 11.026077 | 11.032476 | -0.006400 | -0.007997 / -0.007372 / -0.003831 |
| control-shared | 11.041149 | 11.040439 | 0.000709 | 0.002350 / -0.000932 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

完整宿主11.026077→11.032477 ms，平均慢0.006400 ms，三组都慢0.003831–0.007997 ms；同路径控制组差在-0.000932至+0.002350 ms，未见提速，不追加确认。数值检查通过，SFU减半不足以抵消交换/选择开销。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswin32 SPIR-V：`572651948aaf9c0daf2a2ea2f0a3669065b4a6824d24c7d0827cbe290f8a426a`。
- disabled / fswindsp32 SPIR-V：`1287c97821396cc94440153887242fac8b9b236eae8c351962356cca56d20baf`。
- disabled / fswinfusedup32 SPIR-V：`a470ad407749a336b3ac7a721a23fc285769d51976424a9490784e535cfcd243`。
- shared / fswin32 SPIR-V：`a6731a30d4bcf363852b2ed856330bc3b6b26ae5442e66a38531d4bb8b0e5009`。
- shared / fswindsp32 SPIR-V：`a159fd51ea6ee91b5c9e8db489134a7f003be5922cc2b9d2c14a375b020fe80c`。
- shared / fswinfusedup32 SPIR-V：`0f8e6cca30c8cf1d70641c14ab28cf2c60e5be6ac4633b3138cd35e3d497f027`。
- prototype.patch：`ce4f2238aad7b738ec8b59df49d95bae1fe76ac9369d0388801bbf8566e0bb9a`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查Swin有符号四项边界判断能否用两个无符号比较合并，保持所有有效/越界判定。
