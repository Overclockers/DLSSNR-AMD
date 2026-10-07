# EXP-063：FP16 量化范围的整数位域限幅

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-003019-five9070xt` 第 3/5 个假设；3 版实现，0 次追加确认。
起始主仓 `cb6efa99ca9a4c20d8297f9443572485d99ee25b`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

尝试把已舍入 half 的幅值限制写成 abs 位域与 0x5f00（448）的 uint16 min，再恢复符号；保留每个舍入和 FP8 转换边界。EXP-004 已否定部分浮点限幅写法，本项测试不同的整数指令机制。只在私有副本修改 nr_quant4_h，筛选 gemmvact / gemmprojws。

第一版 mode 4 的直接整数替换在穷举 65,536 个 half 编码、四个变化向量位置（262,144 值）时出现 4,092 个 NaN 输出差异，不能采用。第二版让任意非有限分量回退整向量原路径，通过相同穷举。但是随后核对发现两个目标模块默认使用 mode 1，第二版的驱动 .text 与原版完全相同；ab-integer-fallback / control-integer-fallback 表格只保留为误接路径的同机器码测量，不能作为优化证据。

第三版把相同有限值整数机制接到实际 mode 1，非有限向量完整回退 mode 1 原路径（不加入 mode 4 的 x*0）。新建 mode 1 独立原版探针，262,144 个 GPU 转换全部一致；关闭宏的两个 SPIR-V 与当前基线逐字节一致。整网 FP32 照片也精确一致。

驱动 .text：gemmvact 13,312→20,736 字节，gemmprojws 4,992→7,040；新增整数 mask/打包以及 EXEC 分支控制。新增的数值类保护抵消了限幅表达式的简化。源/probe/SPIR-V/output 身份与全部错误样本保留于私有证据。
独立探针最初缺 GL_EXT_spirv_intrinsics，修复夹具一次，未计为实现变体；失败日志保留。
只读 ADL 每 0.5 秒采集时钟、温度和活动率，覆盖初始化/预热，未与正式帧严格对齐。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.625679 / 10.650643 / 10.664368 / 10.666902 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-integer-fallback | 10.691177 | 10.693355 | -0.002178 | 0.008452 / -0.006694 / -0.008290 |
| ab-integer-mode1 | 10.696373 | 10.719492 | -0.023119 | -0.032801 / -0.001403 / -0.035155 |
| control-integer-fallback | 10.763484 | 10.763165 | 0.000320 | 0.001148 / -0.000509 |
| control-integer-mode1 | 10.767959 | 10.762733 | 0.005226 | -0.002164 / 0.012616 |

实际 mode 1 初测 10.696373→10.719492 ms，平均慢 0.023119 ms，三组全部更慢；同路径控制最大绝对组差 0.012616 ms。判为 regressed，不保留；无需用追加确认寻找有利批次。前三版及无效接入记录全部保存。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 2 份整网 standalone 数值结果，其中 2 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / gemmvact SPIR-V：`d2f933764c49db726f35d45f7c8cff8cabbf75b9842d28d896e98499d910a9d2`。
- disabled / gemmprojws SPIR-V：`63449ee64f848b4d78056350362052a10e279f795dc3788bfee7bc0cb118f636`。
- disabled-mode1 / gemmvact SPIR-V：`d2f933764c49db726f35d45f7c8cff8cabbf75b9842d28d896e98499d910a9d2`。
- disabled-mode1 / gemmprojws SPIR-V：`63449ee64f848b4d78056350362052a10e279f795dc3788bfee7bc0cb118f636`。
- integer-fallback / gemmvact SPIR-V：`a0d4db890a29b5a0c96f70db01e6636e58487fbb008f7af5b215fcd0fa2c6484`。
- integer-fallback / gemmprojws SPIR-V：`98e8519c2c9430a5ad217e7732323186acb8a672f2eaa7c82e9871ef19f8c782`。
- integer-mode1 / gemmvact SPIR-V：`4ba4527ee1209e4dec15b3b2b0bf0891d3bfe6767bff1ad88b0ee1b95510b62c`。
- integer-mode1 / gemmprojws SPIR-V：`e8c70baaa61f117bd71160089527896e9d33bf97a23fd065a982459f8bc285b9`。
- prototype.patch：`7f0aaea729d442c733540d6054c83a5046307d14db743dfc6d93db8c2c04306a`。
- v1.patch：`f14c251732b15a755ef321b29675c1db311710c2482463abd4a3d7eb11dc8846`。
- v2.patch：`648dced8f56cd147bd64835788583b51f896c3fc8895eef71eb5b5b22aa0da22`。
- v3.patch：`6a14e7666a73f474ba80df9ab3e3ac9364bde6bd86d379072cf47bf2fe39764f`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查相邻不同 kernel 的纯 buffer descriptor 是否兼容，在完整保留 pipeline/push/dispatch/barrier 的条件下测试绑定复用。
