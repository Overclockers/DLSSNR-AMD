# EXP-106：C32 下采样池化的成对 y 邻居交换

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（correctness_failed）。
运行 `20261008-193306-lossless9070xt` 第 10/10 个假设；2 版实现，0 次追加确认。
起始主仓 `3c3cadccaaa2d5c3ea4341821fef4f73ac6787e4`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

C32 mode-7 池化原先将两个 y 邻居的 FP32 小计分别 shuffle 后窄化为 half。基线 ISA 已把 shuffle 合入两条扩展 DPP 转换指令，假设先窄化同一对值、打包为一个 uint、交换后解包可暴露更短的成对指令序列。仅 fswinimagepreds32 和 fswindsp32；保留原 x 方向 FP32 加法、原 half 加法、FP32 ×0.25、量化、存储与同步。persistent DS 使用 mode 5，本次没有改它。它不同于 EXP-033 的 Q/K 归一化和 EXP-091 的已量化 LDS 写入。邻近旧实现有编译器改变舍入的先例，因此先做严格探针。第 2 版只交换同一次 half 加法的源码操作数拼写，尝试恢复驱动的 NaN 操作数选择；仍须逐位相同。

两个禁用模块的 SPIR-V 精确复现 baseline；两版候选均构建成功。GPU 只执行独立 shader、独立 dispatch 的探针，完整 32-lane subgroup、local_size=256，并对 half sum 位和两个 FP32 quarter-scaled mean 位比较。每版 1,097,728 个输出记录：direct-half 覆盖全部 65,536 种 half 位型，72 条记录不同；direct-f32 的 196,608 个中点邻域和 65,536 个固定种子随机位记录共 262,144 条，768 条不同；带原 x 方向 FP32 加法和 mode-7 通道选择的 pool-f32 共 262,144 条，774 条不同。另 507,904 条 pool-boundaries 以 half 中点 ± 四分之一 FP32 ULP 构造双舍入敏感输入，零差异。逐一分类的差异全部是 NaN payload 或符号；有限值、数值类别和 lane 标识没有差异。两版所有探针输出彼此逐字节相同，因此交换源码操作数没有修复。第 1 版源 SHA256 `9cbff011c4ab7f65883ac3d0be8567d25d5fccb33cbf0d44407b8fa0764f5108`，第 1/2 版结果 SHA256 `6a75f122a3f3d7ab1c015c18d213fcafc1c7e93339ccd5d6dcb0d9835b5c63f0` / `a7cecad2fba205ace097d43f970b4861cd1ab8196caf1a942358087e8bda5cb3`，差异分类 SHA256 `5b0678e05e55e8ae3e8d954874e6d6eb3f3dc26b2c718f390470d617fa02447d`。例如 direct-half 的记录 31748 低半字为 baseline 0x7e11、candidate 0x7e04；两者都是 NaN，但位模式不同。现象符合 NaN 操作数选择变化，未进一步用候选驱动 ISA 确认具体指令原因。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
本项没有运行 standalone；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.999239 / 11.010161 / 11.007372 / 11.000571 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

两版都未通过事先规定的严格位一致性门槛，判为 correctness_failed，不保留。没有把 NaN 位差异放宽为可接受误差，也没有运行完整 NR 性能筛选或追加确认；本项没有可报告的省时数字。

本项仅执行上述数值 GPU 探针，未运行整网 standalone、完整宿主计时或回读，也未做候选驱动 ISA、池化真实中间值、另一套配方、其它格式/边界尺寸、运动历史、replay、lifetime、fallback 和完整宿主集成验收。上述目标环境及起点宿主基线是本 run 的继承信息，不表示这些整网检查在本项通过。短探针未采样时钟/温度，不作性能结论。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / fswindsp32 SPIR-V：`1287c97821396cc94440153887242fac8b9b236eae8c351962356cca56d20baf`。
- paired / fswinimagepreds32 SPIR-V：`4e2f098d2f375a4083841ffde06dd6b4650b4d3f4e9311484453a9591e04c1bf`。
- paired / fswindsp32 SPIR-V：`fa066f8dfac9b9899c41711e2287278ffc3a580f8d153c8d714e67c7c4059a63`。
- paired-order / fswinimagepreds32 SPIR-V：`3e0fa825d12c4f4ccfef251485a20ea5b52e905bd130ac0a34baf3ac32d06a2a`。
- paired-order / fswindsp32 SPIR-V：`3b15aff0b47d27ede695de2a0a906738d316afa3cb1aa5391d05ed92127c417a`。
- prototype-order.patch：`0c0aa696a9507e42c69df071872eef3e494665b444eb5f06ced6c141c182d0cb`。
- prototype.patch：`86801c8f6fea296be05ca43f1e09dce6f3adfae0ae5f85380194cc1e33bffe23`。

```text
glslang -V --target-env vulkan1.3 -DCANDIDATE=0 -DINPUT_HALF=<0-or-1> -DPOOL_CONTEXT=<0-or-1> <probe.comp> -o <baseline.spv>
glslang -V --target-env vulkan1.3 -DCANDIDATE=1 -DINPUT_HALF=<0-or-1> -DPOOL_CONTEXT=<0-or-1> <probe.comp> -o <candidate.spv>
probe.exe <baseline.spv> <uint4-input> <baseline-output>
probe.exe <candidate.spv> <uint4-input> <candidate-output>
```

随机 FP32 位型使用 Python Random(106)。两臂使用完全相同的 uint4 输入，各自独立 pipeline/dispatch，按输出 uint4 逐字节比较；探针每条输出为打包 half sum、两个 FP32 mean 位和 lane 标识。原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：第十项提交完成后执行本轮最终身份、分支、提交和暂存区核验，以 budget_exhausted 结束本 run；新的实验需要新的明确预算。
