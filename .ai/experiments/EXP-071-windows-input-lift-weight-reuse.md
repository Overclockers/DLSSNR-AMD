# EXP-071：Image pre 的输入投影权重复用

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261008-115341-shapes9070xt` 第 6/10 个假设；1 版实现，0 次追加确认。
起始主仓 `ce870a06b97375159d58470ae6414fe09c351265`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

输入投影的两个 FP16 权重 fragment 在两个 M fragment 上相同。候选把它们加载到寄存器数组后复用，保持每个 WMMA、半精度输出边界、越界 merge、输入特征、量化和同步不变。同时编译普通 pre 与宿主真实使用的 runtime_preencode。区别于输入特征 LDS 布局和读取宽度，目标是消除同一 wave 内重复的不可变操作数加载。

普通 pre 和 runtime_preencode 的禁用 SPIR-V 都精确复现基线。目标 standalone FP32 全图、两份宿主 packed 输出逐字节一致。普通 pre 指令 3509、.text 25856 B、124 VGPR/45 SGPR 不变；匹配创建条件的 runtime probe 同样保持 3577 指令、26240 B、124 VGPR/46 SGPR。两套各类 load 指令计数均不变、6144 B LDS、零 scratch。机器码调度和寄存器编号有变化，因此不是相同机器码。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.862125 / 10.870288 / 10.903622 / 10.937338 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

显式权重复用没有减少驱动实际读取，基线已实现相同加载数量，原定机制未实现。提前关闭为 mechanism_not_realized；没有做完整宿主 A/B，不宣称这些调度差异绝对没有影响。三帧诊断与两次三帧正确性回读不作为性能证据。

保存 2 份宿主回读、6 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- reuse / fswinimagepreds32 SPIR-V：`6f6c3d0964cca08b67c9efcd5e082ca1009918d45db44d4a858658e306f31576`。
- reuse / runtime_preencode SPIR-V：`bc30c2df399661f100dc0058478a0657e5d5d2a9ade13e1ceb1268633646923f`。
- prototype.patch：`7c0d013b675c7c6f6aa917c25f7ab586f3f2cf820da62ef97af216bd111e6ec8`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查融合输入编码的三个 RGB 分支能否合并常见路径，先逐位验证 FP32 编码函数与边界值。
