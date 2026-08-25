// Pass 2 片元着色器：Lambert 漫反射 × 阴影因子
// 阴影判断：当前片元深度 vs Shadow Map 中记录的最近深度
#version 450 core

in vec3 vFragPos;
in vec3 vNormal;
in vec4 vShadowCoord;

out vec4 FragColor;

uniform vec3  uAlbedo;
uniform vec3  uAmbient;
uniform vec3  uLightPos;
uniform vec3  uLightColor;
uniform float uLightIntensity;
uniform float uShadowBias;           // 比较前从片元深度减去的偏移，减轻 acne
uniform sampler2DShadow uShadowMap;  // 需纹理开启 COMPARE_REF_TO_TEXTURE

// 返回 1 = 完全照亮，0 = 完全在阴影中（硬件 PCF 时可能是中间值）
float shadow_factor(vec4 shadow_coord)
{
    // 裁剪空间 → NDC（透视除法）
    vec3 proj = shadow_coord.xyz / shadow_coord.w;
    // NDC [-1,1] → 纹理坐标 / 深度 [0,1]
    proj = proj * 0.5 + 0.5;

    // 落在光锥/正交体之外：当作不被遮挡（配合 border depth=1）
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    // sampler2DShadow：比较 ref 与 texel 深度
    // COMPARE_FUNC=LEQUAL → ref <= stored_depth 则通过（不被挡）
    // bias：故意把片元深度稍微“拉近光源”，减少深度精度/面元偏移造成的自阴影
    return texture(uShadowMap, vec3(proj.xy, proj.z - uShadowBias));
}

void main()
{
    vec3  N      = normalize(vNormal);
    vec3  L      = normalize(uLightPos - vFragPos);
    float diff   = max(dot(N, L), 0.0);   // Lambert
    float shadow = shadow_factor(vShadowCoord);

    // 环境项不受阴影影响；漫反射乘 shadow
    vec3 lighting = uAmbient * uAlbedo
                  + diff * uLightColor * uLightIntensity * uAlbedo * shadow;
    FragColor = vec4(lighting, 1.0);
}
