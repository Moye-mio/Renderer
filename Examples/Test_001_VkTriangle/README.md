# Test_001_VkTriangle

基于 `TitusRHI::IRenderPass` 的三角形示例，默认 Vulkan，亦可 `--backend=gl`。

与 `Test_000_UnifiedTriangle` 同类：只 include `RendererInterface/*.h`，经统一 Pass 调度渲染。

```
Test_001_VkTriangle.exe              # 默认 vk
Test_001_VkTriangle.exe --backend=gl
```

详见根 [`README.md`](../../README.md) 与 [`Docs/Architecture/00_Overview.md`](../../Docs/Architecture/00_Overview.md)。
