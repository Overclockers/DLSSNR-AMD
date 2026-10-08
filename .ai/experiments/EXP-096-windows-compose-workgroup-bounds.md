# EXP-096：合成工作组内部区域跳过逐像素边界检查

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-162203-lossless9070xt` 第 10/10 个假设；3 版实现，1 次追加确认。
起始主仓 `967b51a3cff5d62789943e812dac71b92053558d`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

原packed合成ISA在每个8×8工作组执行两条向量比较和EXEC筛选。目标44997个组中44556个完全在图内。原型先按工作组编号与floor(width/8)、floor(height/8)做一致判定；完整组直接执行原合成，边缘组执行逐像素检查。对任意local∈[0,7]，g<floor(extent/8)保证8g+local<extent；其他组保留相同有效像素集合，包括额外dispatch组。FP表达式、舍入、像素采样/存储、格式和dispatch尺寸不变，与 EXP-090 的Swin有符号/无符号重写不同。

第1版group被LLPC展平：原向量比较仍执行，.text5376→5504B、静态行885→896；第2版加入dont_flatten提示，驱动完整.text与第1版相同（SHA256 `59a64e58d5781efbe775faeed736edc1863c8c69ebd281f1e44cbb2b8c4db237`）。这两版仅做构建/ISA筛选，未声称数值或性能验收。第3版将边缘两个轴的等价整数比较放进dont_unroll循环，内部组机器码确实跳过该循环；.text5632B、静态行933，32VGPR/31SGPR/无LDS/零scratch不变。三次关闭构建均精确复现FP32和packed模块。独立GPU合成probe覆盖12个尺寸、各轴全部模8余数、1×1/对齐/奇数边界、线性与passthrough、controls、模型尺度1/0.5、FP32原地与独立输出、R11G11B10输出和额外guard工作组：480对输出、7,282,688个uint32比较、1,592,304个guard字均精确相同。proof SHA256 `75a4d369a727c622572ec1170299bca9d177dd091cade6c512f4dedcab7736d0`。目标完整packed宿主输出全部一致；仅runtime合成shader改变，没有运行不覆盖它的standalone网络测试。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
本项没有运行 standalone；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.019102 / 11.015484 / 11.018076 / 11.025782 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-loop | 11.055728 | 11.057938 | -0.002210 | -0.002114 / -0.008173 / 0.003657 |
| ba | 11.098029 | 11.108462 | -0.010433 | -0.004876 / -0.012960 / -0.013462 |
| confirm-control | 11.100814 | 11.099177 | 0.001637 | 0.000736 / 0.002538 |
| control-loop | 11.081540 | 11.073887 | 0.007653 | 0.018284 / -0.002977 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

第3版初测11.055728→11.057938 ms，慢0.002210 ms，方向混合且初测控制最大组差0.018284 ms。唯一反序确认11.098029→11.108462 ms，慢0.010433 ms，三组分别慢0.004876/0.012960/0.013462 ms，均大于反序同路径最大组差0.002538 ms。确认显示完整路径退步，regressed，不保留；三版和唯一确认 allowance 已用完。

ADL 合计 624 个有效样本，核心时钟 77–2753 MHz，edge 37–59°C，hotspot 37–83°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
已完成上述独立合成格式/边界/原地输出与guard验证，以及目标静态宿主输出。性能未通过，因此没有进一步运行其它完整宿主输入格式、目标运动历史、多遍、replay、fallback、生命周期或两仓发布构建闭包；局部probe不替代这些验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- branch / runtime_transfer SPIR-V：`f14bb1cc73656816864938b2825bb9addf2e12cedb63c9ac6291cf5b555bb656`。
- branch / runtime_transfer_packed SPIR-V：`1b0a6c57338cb3f44bbe345dcb556ecfb5518f0847bf6d25d260f8cb6888f415`。
- disabled / runtime_transfer SPIR-V：`efd839a430e302878651c77844df80a8a561701d0a510af61c4cf9686e8966b3`。
- disabled / runtime_transfer_packed SPIR-V：`7f25cb1fac16344f1a49d170d51f24297d4c041009400339ed9f61fa888022e0`。
- disabled-branch / runtime_transfer SPIR-V：`efd839a430e302878651c77844df80a8a561701d0a510af61c4cf9686e8966b3`。
- disabled-branch / runtime_transfer_packed SPIR-V：`7f25cb1fac16344f1a49d170d51f24297d4c041009400339ed9f61fa888022e0`。
- disabled-loop / runtime_transfer SPIR-V：`efd839a430e302878651c77844df80a8a561701d0a510af61c4cf9686e8966b3`。
- disabled-loop / runtime_transfer_packed SPIR-V：`7f25cb1fac16344f1a49d170d51f24297d4c041009400339ed9f61fa888022e0`。
- group / runtime_transfer SPIR-V：`e18c6e0428333ad668c735ffbf91f64e0e0b1e12d2caa063dac2f1e7df189105`。
- group / runtime_transfer_packed SPIR-V：`bc4b4cd474d4022551b670aa5a4c4942a67c58999c700b7143875d8432ffb602`。
- loop / runtime_transfer SPIR-V：`b23d7a2ef8c4019ca0812e1c10cf9ab5b49ab212701fe83bffa4a6954a27d184`。
- loop / runtime_transfer_packed SPIR-V：`c249b923b0e205b81bd97399e1947b008c64eb258e2b1afe6a66cd46df84dacf`。
- branch-prototype.patch：`e7703e2e0c44867f24111cf668d3ed905932f67103b7d60cda3f817661150c32`。
- loop-prototype.patch：`ef4144a74155139e62b302bd87e7bc33d299d1a4e279ed0aee3dd55878e953f9`。
- prototype.patch：`e7533dad0c3cb4381dc29a5d1db023943ccff336e039b81cdcca95176481263f`。

```text
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮10项假设已达到上限；完成提交、源/产物身份与两仓分支/索引核验，将ledger关闭为budget_exhausted，不启动新实验。
