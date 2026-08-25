// Pass 2 顶点着色器：主相机渲染，同时输出光空间坐标供阴影采样
#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;  // 本 demo 未用，但 Mesh 顶点布局含 uv

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
// 只有 light_proj * light_view，不含 model
// 世界坐标对主相机 / 光源是同一份，所以 shadowCoord = uLightVP * world
uniform mat4 uLightVP;

out vec3 vFragPos;      // 世界空间位置（算光照方向）
out vec3 vNormal;       // 世界空间法线
out vec4 vShadowCoord;  // 光空间裁剪坐标，片元里透视除法后与 Shadow Map 比较

void main()
{
    // 局部 → 世界（只做一次）
    vec4 world = model * vec4(aPos, 1.0);
    vFragPos = world.xyz;

    // 法线变换：忽略非均匀缩放时的简化写法；正确做法用 normal matrix
    vNormal = mat3(transpose(inverse(model))) * aNormal;

    // 世界 → 光空间裁剪坐标（不要再乘 model）
    vShadowCoord = uLightVP * world;

    // 世界 → 主相机裁剪空间 → 画到屏幕
    gl_Position = projection * view * world;
}
