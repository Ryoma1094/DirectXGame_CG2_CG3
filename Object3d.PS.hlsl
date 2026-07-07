struct VertexShaderOutput{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct Material{
    float4 color;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);

struct PixelShaderOutput{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input){
    PixelShaderOutput output;
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord).bgra;
    output.color = gMaterial.color * textureColor;
    return output;
}