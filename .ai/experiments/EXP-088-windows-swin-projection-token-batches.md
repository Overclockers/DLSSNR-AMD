# EXP-088：C256 DS 的 attention 输出投影按 query 对分批

日期：2026-10-08（Asia/Hong_Kong）。结论：保留，收益很小。
运行 `20261008-162203-lossless9070xt` 第 2/10 个假设；1 版实现，1 次反序确认。
起点主仓 `35a89b15aa19e973fe64327684568a55ac4036ff`，宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。
宿主提交 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`，customer-mochi-backed；主仓 main-experiment 为本提交。

## 机制与数值不变量

C256 DS 原有 229 VGPR，stage 6 同时保存 2×4 份投影累加器。
仅在 fswinpds256 配方启用 NR_PROJECT_BATCH=2，先完成 m=0,1，再完成 m=2,3；
每份累加器保持 residual 初始化、成对 N 权重、升序 K、FP32 MMA、half/FP8 转换与最终写入。
没有改变 expansion、QKV、attention、模型、精度或归约顺序。
两次原型预处理/作用域修复均在取得有效构建前，未计作额外变体；失败日志保留。

每批两行 residual 全部读完才允许池化覆盖；pooled LDS 仅占 m=0 区域，不碰后续 m=2,3 或其他 head。
对 C64/128/256 共 14 个 head 布局枚举证明地址不交叠；本次只启用 C256 DS。
窗口有效掩码只删除访问，不增加别名。重排区域内没有 barrier；原最终 DS barrier、队列发布与领取不变。
编译检查限定四 query tiles、head-wave、paired dense 和 DS projection；关闭恢复原路径。
ABI、weight packing、host defines、markers 与资源接口均未改变。

## 完整离线 NR 测量

RX 9070 XT，AMD proprietary 26.8.1 (LLPC)，Windows driver 32.0.31041.1004；glslang 16.5.0、MSVC 14.51。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches；100% 尺度、1 NR pass。
完整宿主线性 R11G11B10、depth=3、静态照片、无运动历史/SR/FG 性能负载；standalone FP32 用于诊断。
从 D3D12 输入复制计到输出复制，包含 Vulkan 往返。
每次 A/B 前基线运行 4000 帧统一预热，随后 3 帧只验输出；每个正式进程预热 100、测量 200 帧。
ABBA×3、BAAB×3，各紧随同路径控制×2，共 8000 个正式帧；未选择性删样。

| 比较 | 基线 ms | 候选/控制 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-batch2-fixed | 11.035285 | 11.022791 | 0.012494 | 0.004623 / 0.020948 / 0.011912 |
| ba | 11.099342 | 11.092038 | 0.007304 | 0.000633 / 0.008689 / 0.012591 |
| confirm-control | 11.093651 | 11.093093 | 0.000559 | 0.001724 / -0.000607 |
| control-batch2-fixed | 11.053555 | 11.056769 | -0.003214 | -0.004304 / -0.002124 |

六组候选均值全部同向；两次总均差均超过对应控制组差值范围，但确认第一组落在控制波动内。
初测省 0.012494 ms，确认省 0.007304 ms；登记较小批次均值 0.007304 ms，约 0.07%。
这不是保证下界，也不与旧会话时间跨批相减或推断游戏 FPS。
预期的 VGPR 下降未发生：229 VGPR、107 SGPR、32768 B LDS、零 scratch 不变。
driver .text 224384→224512 B，静态指令 31956→32006；收益归于实测调度变化，不声称占用率提升。
ADL 共 608 个 0.5 秒只读样本，含初始化/预热、未与帧同步；gfx 77–2863 MHz（中位2648），
edge 36–59°C、hotspot 37–84°C；低频样本包括进程间空隙。未调频或检查/更改其他进程。

## 精确验证与集成

- 目标 FP32 输出和全部 packed 性能末帧与独立基线逐字节一致。
- 主仓/宿主两套配方各 15 对生命周期回读、6 对 gradient/随机/HDR/奇数/小尺寸输出一致；
  双队列、replay、DRS/resize、历史排序、延迟回收、取消/销毁与 pass switching 通过。
- 两套配方各 36 对目标历史与 90 对格式输出一致；覆盖 13 种底层 VkFormat、非零亚像素 motion、
  变化 alpha、连续/丢弃/reset/乱序/replay。DXGI 别名沿用未改映射，没有逐别名单测。
- 两套配方各 4 对边界输出一致：1920×1080、1920×1536、2368×1280、3840×2160；
  另含2257×1269、129×65、17×19。极小不可用的原基线路由沿用 EXP-076 限制，未声称验证。
- 对两套配方，在原 barrier 后从实际最终 pooled LDS 回读至独立 pool slot，2258×1270 与129×65
  共4对中间值逐字节一致（每对778240或36864字节）；诊断完整输出也一致。
  使用 --no-reuse 保证 pool slot 独立，原 --dump-layer 命令遗漏显式 arena flag 在初始化检查被拒，修正后通过。
  诊断插桩未用于性能，源码和输出保留忽略目录。
- 三套全量 shader 构建各65模块；各配方内仅 g_fswinpds256.spv 改变，其他模块/markers相同。
  因两仓既有展开器差异，主仓前后模块在同一已验证宿主资产内分别替换测试；
  没有声称完整主仓配方全模块组合的 GPU 验收。非DS256 fallback 模块未变。
- 两仓最终源码补充约束与说明后，开/关各自再次编译：开启与实测模块精确一致，关闭与各自原基线一致。
  C++/ABI未改，复用身份核验的 production DLL。Linux未改；没有游戏、安装、部署、发布打包或推送。

## 身份与复现

- host-before/g_fswinpds256.spv：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- host-after/g_fswinpds256.spv：`3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad`。
- main-before/g_fswinpds256.spv：`811d05b5b84409f7e13863f619a49eaa48f081154fc8f35514dd3011006dde29`。
- main-after/g_fswinpds256.spv：`53b70e7694185dea31733f9d3f4ee4d8bc42ce1b06e28e93fb0b99100e87f6c6`。
- alias-proof.json：`8c4e1c8cccbfe0191ca47cc1bd20fe61d46b7093828918416b36e844c2afca31`。
- pool-validation.json：`7acae37014a9859e611d85a419505f5671bd9c221711916400808ff54f63a272`。
- build-closure.json：`b361fe08b89de71129f3bb0d3be617c0a84f1768769e278869f3b0e0e7da0863`。
- validation.json：`3751755266b62ea9daadea570fd5ba6c2a3963d618e01e64490d88a4eb2a646c`。
- validation-main-after.json：`6f8e13d304bbf7134fd9f021dfff5fcf816449d5822077701721c14fbe756c10`。
- target-validation-batch2-fixed.json：`e30b39979621396561c76c5046425a1abcc5cfe178acf43f19721147556e5ad0`。
- format-validation-batch2-fixed.json：`6594469fb17e200e63e1d9bb6af54101e26286ceacc0251aa9ef949f0fa9c110`。
- target-validation-main-after.json：`cac9d66a22b85c835bccc3dcec3098b67871f5b679473241be5c3aa7771aceab`。
- format-validation-main-after.json：`5df3bd4d666ffad52ed035bc6154e5985968ed615cab63242b9072eabe702a29`。
- edges-validation-batch2-fixed.json：`1e95667ea8c3fd9fe598e766920b0262f85d8316fa4ade1ed3719082782668e9`。
- edges-validation-main-after.json：`c788618eae3b7d78ddddaf7fae2acffee419ed19e8ca6b82c4bfcef108b72821`。
- retained-final.patch：`1c8ec013ed47b5cad971e9efc423dc7a92bb8810bd2f3884d69717c0145513c5`。
- telemetry-summary.json：`4b0ad67b8c67f5e30938d264f822bf492b698d3ede7fdd4a920bc97de479156d`。

```text
python -X utf8 windows/build/build_network.py rdna4 --out <shaders> --glslang <pinned-glslang>
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
nr_benchmark 2258 1270 <plan> --no-reuse --model-pack <model> --spv-dir <instrumented-shaders> --dump-layer 22:6 --dump-layer-to <pool> --out-image <rgba> --warmup 1 --repeats 2 --in-image <photo>
target_history_v2.exe <production-dll> <assets>
target_formats.exe <production-dll> <assets>
```

私有模型、原始CSV、回读、源码快照、诊断工具与ISA留忽略目录；本机定位见local ledger。
下一项检查C32 Q/K规范化中重复的reciprocal-square-root工作。
