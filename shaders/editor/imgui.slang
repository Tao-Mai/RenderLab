struct UiParameters { float2 scale; float2 translate; };
ConstantBuffer<UiParameters> parameters;
Texture2D<float4> sourceImage;
SamplerState linearSampler;
struct VertexInput { float2 position : POSITION; float2 uv : TEXCOORD; float4 color : COLOR; };
struct VertexOutput { float4 position : SV_Position; float2 uv : TEXCOORD; float4 color : COLOR; };
[shader("vertex")] VertexOutput vertMain(VertexInput input)
{ return {float4(input.position * parameters.scale + parameters.translate, 0.0, 1.0), input.uv, input.color}; }
[shader("fragment")] float4 fragMain(VertexOutput input) : SV_Target
{ return input.color * sourceImage.Sample(linearSampler, input.uv); }
