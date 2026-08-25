// Pass 1 顶点着色器：把顶点变换到「光源相机」的裁剪空间
// 输出的 gl_Position.z/w 经光栅化后写入深度缓冲 → 形成 Shadow Map
#version 450 core

layout (location = 0) in vec3 aPos;

// light_proj * light_view * model（CPU 端已乘好）
uniform mat4 uLightMVP;

void main()
{
    gl_Position = uLightMVP * vec4(aPos, 1.0);
}
