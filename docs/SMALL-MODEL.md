# CC_SMALL_FP8 on RX 9070 XT

Validated locally on 2026-10-17 (Asia/Hong_Kong), on Windows and an AMD Radeon RX 9070 XT.

`nvngx_dlssnr.dll` 310.9.16 contains two resources. `WEIGHTS_HT` rebuilds the existing HT model
byte for byte. `WEIGHTS_CC_SMALL_FP8` contains the new, smaller model. The default extractor selects
Small for this DLL and HT for 310.8.0; `--model ht` selects HT explicitly.

| | HT | CC_SMALL_FP8 |
| --- | --- | --- |
| Blocks | 71 | 31 |
| Layers | 152 | 76 |
| ViT blocks | 8 | 8 |
| Packed entries | 599 | 256 |
| Packed bytes | 147,756,560 | 113,917,442 |
| Packed SHA-256 | `2b41c888cf4155b8958c665ba64018ab0bd25c85fc71a2b6db86d0d04d1f7fbd` | `dc7edf0c5da0af3c3b2048bafa72f928f4f2eb89f117a37bc4c8863bb2645d8c` |

The known 310.9.16 DLL SHA-256 is `2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f`.
The Small resource SHA-256 is `f743e8eb320f42bc9bdc992ee2bf51f7bb7c8688ced4ecf5ce6807ec4eef7962`.
No DLL, weight values, model pack or input/output frame is included in the source tree.

## Implementation

Small reduces the repeated blocks at every encoder/decoder width. Channel widths and the eight
1024-channel ViT blocks remain. It uses the existing RDNA4 FP8 kernels, with the existing FP16/FP32
boundaries and FP32 accumulation; no additional quantization is introduced.

`linux/package/model-tools/descriptor-small.json` records the final FP8 descriptor, reconstructed by
CPU emulation of the 310.9.16 descriptor-list factory at RVA `0x602e0` and its base factory at
`0x5c5a0`. Neither the DLL loader nor a CUDA/GPU backend is invoked. The descriptor supplies the
actual layer names, channels, signed window shifts, six skip edges and output indices. Its own
checksums and generated extraction tables verify all 256 packed entries.

Both native planners contain the 31-block graph. Installed runtimes identify it from the model index.
ViT input/output repacks follow the graph's actual ViT endpoints (blocks 11–18 for Small, 31–38 for HT).
Temporal composition reads Small's post-block `blend_scale` from the pack; it differs from HT's
verified legacy constant. The installed INT4 recovery tables were calibrated for HT, so Small uses its original
FP8 weights even when an old configuration requests INT4. ACO selection is retained on Windows.

## Measured network cost

Windows driver compilation, ACO off, the same FP8 shader bundle and one static game image for both
models. Each resolution used alternating HT/Small order, three runs per model, 100 warmup frames and
200 measured frames per run. Values below are medians of Vulkan GPU timestamps. ADL telemetry was
sampled read-only; its samples include startup/warmup and are not synchronized to individual frames.
Clocks were not locked.

| Source resolution | HT | Small | Less GPU time | Network speedup |
| --- | ---: | ---: | ---: | ---: |
| 1920 × 1080 | 7.685 ms | 4.673 ms | 39.2% | 1.64× |
| 2560 × 1440 | 13.408 ms | 7.982 ms | 40.5% | 1.68× |
| 3840 × 2160 | 28.714 ms | 16.940 ms | 41.0% | 1.70× |

The compiled graph has 123 dispatches for HT and 77 for Small with this bundle. This comparison
measures the graph and weights change; it does not measure ACO Mode or INT4 and does not predict a
game's complete frame rate. The ViT bottleneck remains, so block-count reduction is not a linear
prediction of GPU-time reduction.

## Validation and limits

- Python and Windows C++ extraction produce the same Small pack. The original 310.8.0 DLL and
  310.9.16 `--model ht` produce the same HT pack. ZIP input is checked through the Python path.
- Invalid model choices, unsupported DLLs and requesting Small from the old DLL are rejected while
  preserving the output file. Extracted entries are checked against separate HT/Small checksum lists.
- Native-plan checks cover standard and odd dimensions. HT plans are compared with the previous
  planner, and a rebuild of the previous graph provides a byte-for-byte HT GPU-output regression check.
- Full Small inference runs on the RX 9070 XT at 1080p, 1440p and 4K with finite, input-dependent
  outputs. Repeated runs of each model are deterministic with the fixed input/noise settings.
- The actual Runtime API is checked with Small model auto-selection, temporal history, Reset,
  independent features, zero intensity and rejection of HT INT4 calibration. This exercises installed
  layout and temporal shaders, not only the offline graph executable.
- The same Runtime check passes with the legacy HT pack. At 2258 × 1270, Small outputs are byte-identical
  with activation memory shared by lifetime and with separate allocations for every value.

Small is a different trained model: its output need not match HT, and these checks do not establish a
quality budget against HT. No new-model NVIDIA execution or in-game capture was available for this
validation, so exact agreement with NVIDIA's Small runtime and real-game temporal quality remain
unmeasured. Existing HT verification results do not constitute a Small reference comparison.

## Extract and use

Use an updated build containing both the Small planner and graph-dependent ViT routing. Replacing the
model in an older runtime that assumes the HT graph is insufficient.

```sh
bash model-tools/extract_model.sh nvngx_dlssnr.dll dlssnr.bin --model small
```

```bat
model-tools\dlssnr_extract_model.exe nvngx_dlssnr.dll dlssnr.bin --model small
```

The Windows package needs no Python. Its installer also accepts a ZIP. Both installers validate the
cached model and select the corresponding graph; supplying the DLL again refreshes the package cache.
To compare HT with Small, use separate copies of the updated package with the respective model packs
and restart the application between models.
