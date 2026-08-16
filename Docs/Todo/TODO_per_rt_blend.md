# 待办计划：GL 后端 per-RT 混合状态

## 背景

`RendererGL/GLCommandList.cpp`（约 200-220 行）在应用 `BlendState` 时，只取 `attachments[0]` 通过全局混合函数（`glBlendFuncSeparate` / `glBlendEquationSeparate` / `glColorMask`）设置，对所有颜色附件统一生效。

抽象层的 `BlendState`（`RendererCore/GDescs.h:247`）本身是 per-attachment 的数组，对标 VK 的 `VkPipelineColorBlendStateCreateInfo::pAttachments[]`。VK 后端（`RendererVK/VKDevice.cpp:1105+`）已按每个附件独立处理。

## 现状评估（结论：暂不改）

当前项目所有实际用法中，MRT 通道的各 RT 混合状态**完全一致**，`attachments[0]` 足以代表全部，实现无行为缺陷：

| 文件 | 附件数 | 混合设置 |
|---|---|---|
| `000_Forward_Deferred_ForwardPlus/SponzaGBufferPass.cpp` | 3 | 全部禁用混合 |
| `001_Reflective_shadow_map/SponzaGBufferPass.cpp` | 3 | 全部禁用混合 |
| `001_Reflective_shadow_map/RSMBufferPass.cpp` | 3 | 全部禁用混合 |
| `000_Forward_Deferred_ForwardPlus/DeferredLightingPass.cpp` | 1 | 单 RT |
| `001_Reflective_shadow_map/ScreenQuadPass.cpp` | 1 | 单 RT |

## 待办项

- [ ] **（低优先级）加护栏**：在 GL 后端混合逻辑处补充说明注释；`_DEBUG` 下断言多附件混合参数一致，防止 per-RT 不同混合被静默忽略。
- [ ] **（按需）实现 per-RT 混合**：当出现「同一 FBO 多附件需不同混合参数」的需求（如某些 OIT 算法）时，改用 GL 4.0 索引版 API：
  - `glEnablei/glDisablei(GL_BLEND, i)`
  - `glBlendFuncSeparatei(i, ...)`
  - `glBlendEquationSeparatei(i, ...)`
  - `glColorMaski(i, ...)`
  - 遍历 `bs.attachments`，逐个附件下发。
- [ ] **触发条件**：仅当新增通道需要「不同 RT 用不同混合方程/因子」时才实施第二项。

## 参考实现片段（per-RT）

```cpp
for (size_t i = 0; i < bs.attachments.size(); ++i) {
    const auto& a = bs.attachments[i];
    if (a.blendEnable) glEnablei(GL_BLEND, (GLuint)i);
    else               glDisablei(GL_BLEND, (GLuint)i);
    glBlendFuncSeparatei((GLuint)i,
        ToGLBlendFactor(a.srcColorBlendFactor), ToGLBlendFactor(a.dstColorBlendFactor),
        ToGLBlendFactor(a.srcAlphaBlendFactor), ToGLBlendFactor(a.dstAlphaBlendFactor));
    glBlendEquationSeparatei((GLuint)i,
        ToGLBlendOp(a.colorBlendOp), ToGLBlendOp(a.alphaBlendOp));
    glColorMaski((GLuint)i,
        (a.colorWriteMask & 0x1) ? GL_TRUE : GL_FALSE,
        (a.colorWriteMask & 0x2) ? GL_TRUE : GL_FALSE,
        (a.colorWriteMask & 0x4) ? GL_TRUE : GL_FALSE,
        (a.colorWriteMask & 0x8) ? GL_TRUE : GL_FALSE);
}
```

## 版本前提

- MRT 本体：GL 3.0 即可（FBO 多附件 + `glDrawBuffers`）。
- per-RT 独立混合方程/因子：需 **GL 4.0**（索引版 `glBlendFunci` 等）。项目历史代码已用过 `glColorMaski`，说明运行环境满足 GL 4.0。
