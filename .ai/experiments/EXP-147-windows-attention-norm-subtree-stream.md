# EXP-147：C512 Q/K归一化子树即时消费

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261010-224000-ten9070xt` 第 2/10 个假设；1 版实现，0 次追加确认。
起始主仓 `a5eb50b96a8e538ed4a77c918f6394de532f7e23`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

把原八个独立t向量改为成对生成c和c+4，并即时形成u[c]；保持两个half平方、平方相加、XOR16、t[c]+t[c+4]及最终归约树的精确次序。不同于EXP-033的lane交换表示，不减少交换或共享rsqrt。仅在忽略快照中修改C512的QK epilogue。

禁用SPIR-V精确一致；候选整网11,470,640个FP32位模式与独立基线一致。一次GLSL保留词half编译失败，改名side后完成，无额外变体/GPU错误调用。driver .text26880B/3882条指令不变，184VGPR/59SGPR/9216B LDS/零scratch不变。局部ISA依赖追踪（square、move、XOR16、packed加法）定位首个t+t子树：基线在形成4个t后即消费，候选要等8个t全部形成，首个消费位置646→651（原ISA行索引）。这不是完整ISA模拟器，只用于该局部机制门槛；原驱动已有部分即时消费，新源码未缩短所需t存活范围。证据subtree-proof.json与绑定ISA身份保留。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.963855 / 10.966668 / 10.968056 / 10.955780 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

mechanism_not_realized，不保留。源码子树消费没有按预期落到驱动；没有启动候选packed、4000帧候选预热或完整性能ABBA。局部依赖追踪不用于性能周期预测。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / attn SPIR-V：`4bafe70c5fa393acc1b0d6c1f53715a48f1cc6b9adf137d42d23caabdf6c62dc`。
- stream-repair / attn SPIR-V：`503c07e5511289e1fe181ca59ecd8a09475dd61722cc2da552e23b13239352a5`。
- prototype.patch：`5156f7a446f4381a078fdda29e8847a46304a93eddb3737958e578beeac6cd75`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续第3/10项：ViT概率排列的uint条件位交换，保持所有概率和归约计算不变。
