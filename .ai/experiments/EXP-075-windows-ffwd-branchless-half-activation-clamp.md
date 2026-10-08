# EXP-075：C512 half 激活的无分支位域限幅

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-115341-shapes9070xt` 第 10/10 个假设；1 版实现，0 次追加确认。
起始主仓 `52988c639edcedbf0a500d275b477969c11f36d9`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

实际 ffwd3w ISA 对 half 激活用逐分量 v_med3_num_f16 限制至 [-4,4]，然后重新打包给 half FMA。候选对 half 幅值位域取 min(0x4400) 并恢复符号；幅值加 0x03ff 仅在 NaN 编码设置 bit 15，复现原 clamp 的负边界特殊值结果，无运行时 EXEC 回退。原两次 half FMA、最终 half multiply、FP8 量化与所有归约保持。本项针对实际 half 激活，使用新的无分支特殊值处理，解决 EXP-063 原型因非有限值整向量回退导致的控制开销；未改其量化器。

GPU 穷举全部 65,536 个 half 编码并变换配对分量，比较 262,144 个 activation half 和 262,144 个 clamp half 中间值，全部逐位一致，覆盖 NaN/Inf/subnormal/正负零。探针输出 SHA-256 为 4fbb409d9caa67b0abc218b36e892d4d38ce4b6e5e99401f7581ca2df96220c4。可用 probe <shader> <uint4-input> <uint4-output> 复现；源与具体配对保存在忽略目录。ffwd3/ffwd3w 的禁用 SPIR-V 复现基线，目标 FP32 与每份 packed 图一致。目标实际执行 ffwd3w，较小尺寸 ffwd3 尚未 GPU 验证。其 .text 31232→36864 B、指令 4185→4831，182 VGPR/23 SGPR、零 LDS/scratch 不变；256 条 med3 被替代，但掩码、重排、delay 和 128 对 packed min/add 增加，未减少整体代码。ADL 以 0.5 秒只读采样，包含初始化/预热且非帧同步。

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
| ab-bits | 11.001905 | 11.044817 | -0.042912 | -0.033909 / -0.046661 / -0.048167 |
| control-bits | 11.089298 | 11.096507 | -0.007208 | -0.006216 / -0.008201 |

完整 NR 三组全部变慢，平均增加 0.042912 ms；各组慢 0.033909/0.046661/0.048167 ms，控制最大绝对组差 0.008202 ms。关闭为 regressed，不保留、不追加确认。该项完成后本轮已启动/关闭/上限为 10/10/10，停止原因为 budget_exhausted；全部尝试均无可保留的新收益。

保存 22 份宿主回读、4006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- bits / ffwd3 SPIR-V：`9e5c005ba647afdcf6ea17bb9aa13d256d94d0dc055c5afd4cc46e1d064ba464`。
- bits / ffwd3w SPIR-V：`8a4ba92766f1a87626ff2ae41a0f1066e4f5783a8abe20c7f966c3a969681134`。
- disabled / ffwd3 SPIR-V：`498de530c88d09f3a266d4dd2c638749cddc3a3a58cec9827881bc22fad217e4`。
- disabled / ffwd3w SPIR-V：`3ad3da9ee684c7951e5d37a7bca62af1ca9e8357b9abbeabbd2cd662aeaaabdb`。
- prototype.patch：`5b67c666e4b5927354db5d45cf3f0b51b6904d2455c3e05f6f15086be00fbed3`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮预算耗尽，保存和核验全部记录。未来新运行可基于新鲜热点与因果证据选题，不直接重复这些已排除的地址、读取和缓存写法。
