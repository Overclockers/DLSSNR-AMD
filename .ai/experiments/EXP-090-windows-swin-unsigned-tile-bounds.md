# EXP-090：Swin 图块边界的无符号合并比较

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-162203-lossless9070xt` 第 4/10 个假设；2 版实现，0 次追加确认。
起始主仓 `01417b66bd30d06e2a07edda8faf30efcecdaf95`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

对正数且不超过INT_MAX的图块尺寸，把tx<0 || tx>=width等价改为uint(tx)>=width，二维合并为两个比较。负坐标转换后至少为2^31，非负坐标转换不变；保留原坐标算术与clamp地址。49,188个一维边界/极值用例通过，通用源级证明覆盖全int32域。第一版覆盖9模块，第二版仅6个persistent模块，隔离C32机器码增大。

关闭9模块SPIR-V精确；两个变体FP32与全部packed全图精确。C256 DS .text 224512→222720B，静态行32006→31527，VGPR229→230；但普通C32代码38144→38912B、fusedup42240→44416B。所有模块零scratch。bounds-proof.json SHA256 `db2bd06c49f25be0475208e9db64df563b8fb9651f991e07413307621a8c9584`。ADL每0.5秒只读采样，包含启动与预热，未按帧校正。

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
| ab-persistent | 11.062732 | 11.180704 | -0.117972 | -0.116725 / -0.109342 / -0.127848 |
| ab-unsigned | 11.034527 | 11.189868 | -0.155342 | -0.158240 / -0.153673 / -0.154112 |
| control-persistent | 11.064225 | 11.064545 | -0.000320 | -0.000682 / 0.000041 |
| control-unsigned | 11.049399 | 11.048691 | 0.000708 | -0.005483 / 0.006900 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

9模块组合11.034527→11.189868 ms，慢0.155342 ms；仅persistent版本11.062732→11.180704 ms，仍慢0.117972 ms，三组全部退步。两批对应控制均差约+0.000708/-0.000320 ms。代码缩小未转为路径提速，两个版本回退，不用追加确认或第三版。

保存 46 份宿主回读、8018 个測量帧；全部完整输出与独立基线逐字节比较。
保存 2 份整网 standalone 数值结果，其中 2 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswin32 SPIR-V：`572651948aaf9c0daf2a2ea2f0a3669065b4a6824d24c7d0827cbe290f8a426a`。
- disabled / fswindsp32 SPIR-V：`1287c97821396cc94440153887242fac8b9b236eae8c351962356cca56d20baf`。
- disabled / fswinfusedup32 SPIR-V：`a470ad407749a336b3ac7a721a23fc285769d51976424a9490784e535cfcd243`。
- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V：`3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad`。
- disabled / fswinpup64 SPIR-V：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- persistent / fswinpds64 SPIR-V：`9a5a3dc29547123a6ec4eaa497c7af93ab16101dd8c4ada5fb470da5d1b71b7a`。
- persistent / fswinpds128 SPIR-V：`ce4885867568495b5e289b9814c31deff6c02a72dee2782d6d93e7f8ff9e1419`。
- persistent / fswinpds256 SPIR-V：`f15248fcedaf6cb5d9854c1f18abbce30655ded98d2535c62f34e15c0c380144`。
- persistent / fswinpup64 SPIR-V：`8b75c7d3f72e6c64276a07f47386270437483969ca4c884f2e81c3183e2791e2`。
- persistent / fswinpup128 SPIR-V：`21709710d8b930398fb8fa0f7f6744f37775b510c3a39fe473b11a3a81270ad9`。
- persistent / fswinpup256 SPIR-V：`99c6ae995926f3d6d707a853657c0693be87c63a30897f9b993129347db95e6a`。
- unsigned / fswin32 SPIR-V：`ee15c7236e99d5f22bb480c162562a0d563ed5e35dba2deeaa44fdd14af08c20`。
- unsigned / fswindsp32 SPIR-V：`dcdc0a4f95704febc67f37af7df5d59217144b33b5542e4dae2167b0fef31896`。
- unsigned / fswinfusedup32 SPIR-V：`54fa602dd8578df2964a10b04cc1a83966d05d10613da705fb678676790e3378`。
- unsigned / fswinpds64 SPIR-V：`9a5a3dc29547123a6ec4eaa497c7af93ab16101dd8c4ada5fb470da5d1b71b7a`。
- unsigned / fswinpds128 SPIR-V：`ce4885867568495b5e289b9814c31deff6c02a72dee2782d6d93e7f8ff9e1419`。
- unsigned / fswinpds256 SPIR-V：`f15248fcedaf6cb5d9854c1f18abbce30655ded98d2535c62f34e15c0c380144`。
- unsigned / fswinpup64 SPIR-V：`8b75c7d3f72e6c64276a07f47386270437483969ca4c884f2e81c3183e2791e2`。
- unsigned / fswinpup128 SPIR-V：`21709710d8b930398fb8fa0f7f6744f37775b510c3a39fe473b11a3a81270ad9`。
- unsigned / fswinpup256 SPIR-V：`99c6ae995926f3d6d707a853657c0693be87c63a30897f9b993129347db95e6a`。
- prototype.patch：`2299c3cdbf51cb4a27000333bd351c466fe866eb16a68b02ab60710d4d8ed8c9`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：已保存C256 DS ISA明确存在成串8字节标量LDS池化写入；下一项测试按原字节布局打包写入。
