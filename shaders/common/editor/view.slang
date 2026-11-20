#ifndef RENDERLAB_EDITOR_VIEW_SLANG
#define RENDERLAB_EDITOR_VIEW_SLANG

struct EditorViewUniforms
{
    float4x4 viewProjection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<EditorViewUniforms> editorView;

#endif
