# EXP-092：合成 signed cbrt 的精确符号位附加

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-162203-lossless9070xt` 第 6/10 个假设；1 版实现，1 次追加确认。
起始主仓 `c48adaac5627616383ff70c95233c4e57c5f8e97`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

原signed cbrt为sign(v)*pow(abs(v),1/3)。当三个输入都是有限normal数时，pow正结果乘±1等价于XOR输入符号位；零、subnormal、NaN、Inf任一存在的向量保留完整原表达式。pow、OkLab矩阵、controls和所有精度不变。目的是减少驱动原先每分量两次compare/select及一次乘法，非立方根近似。

200,192个RGB向量、600,576个分量的函数输出逐位相同，含全部half编码提升、各float指数/尾数边界和随机位模式；cbrt-proof.json SHA256 `cbba35a25dd1697d27186384d104b28ade42a5bdc39dde297f1113439c321689`。关闭FP32/packed两模块SPIR-V精确还原。packed driver .text5376→6144B，静态行885→1036，32VGPR/31SGPR/无LDS/零scratch不变，因特殊值回退增加代码。网络shader未变，所有完整宿主packed回读一致；未把未改变的standalone网络计时当作合成验证。ADL只读采样含初始化/预热。

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
| ab-signbits | 11.043606 | 11.049690 | -0.006084 | -0.007236 / -0.001800 / -0.009214 |
| ba | 11.093960 | 11.087939 | 0.006021 | -0.001778 / 0.007745 / 0.012097 |
| confirm-control | 11.081241 | 11.095567 | -0.014325 | -0.015102 / -0.013549 |
| control-signbits | 11.060518 | 11.062152 | -0.001633 | -0.010801 / 0.007534 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测11.043606→11.049690 ms，慢0.006084 ms；唯一反序确认11.093960→11.087939 ms，省0.006021 ms，但方向混合且反序同路径控制均差-0.014325 ms（组差-0.015102/-0.013549 ms）更大。未建立可分辨、可重复收益，unproven，恢复基线，不继续追加测量。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / runtime_transfer SPIR-V：`efd839a430e302878651c77844df80a8a561701d0a510af61c4cf9686e8966b3`。
- disabled / runtime_transfer_packed SPIR-V：`7f25cb1fac16344f1a49d170d51f24297d4c041009400339ed9f61fa888022e0`。
- signbits / runtime_transfer SPIR-V：`c5a80b3352b284681ce514b6de96ac427f65779917e3f07b47504b405d5c9c3d`。
- signbits / runtime_transfer_packed SPIR-V：`9db46510106f349740936101da5006113436db7b1c1a6b8471c68ccdb4912fa0`。
- prototype.patch：`a476f4779db8b30b9a23fc1fb666512a6cdc2de49d9ab85b4c1cc6615c9c09e9`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查ViT GEMM的FP8输出片段能否只在已量化字节上做wave转置，以合并原本分散的单字节全局写入。
