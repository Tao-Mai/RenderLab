#ifndef RENDERLAB_SCENE_DATA_SLANG
#define RENDERLAB_SCENE_DATA_SLANG

#include "scene_uniforms.slang"

[[vk::binding(0, 0)]]
ConstantBuffer<SceneUniforms> sceneUniforms;

#endif
