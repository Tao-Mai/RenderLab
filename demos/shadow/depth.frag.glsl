// Pass 1 片元着色器：故意为空
// 深度由固定管线根据顶点插值后的 gl_Position 自动写入 DEPTH_ATTACHMENT，
// 不需要手动输出颜色（FBO 也没有颜色附件）。
#version 450 core

void main()
{
}
