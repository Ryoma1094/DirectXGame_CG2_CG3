#include "Particle.hlsli"

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer MaterialBuffer : register(b0)
{
    Material gMaterial;
};

cbuffer DirectionalLightBuffer : register(b1)
{
    DirectionalLight gDirectionalLight;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // UV変換行列を適用してテクスチャ座標を計算
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    // マテリアルカラー × インスタンスカラー × テクスチャカラー
    float4 baseColor = gMaterial.color * input.color * textureColor;
    float alpha = baseColor.a;
    
    // 2値抜き（αTest / Alpha Discard）処理
    if (gMaterial.enableAlphaDiscard != 0)
    {
        if (alpha <= gMaterial.discardThreshold)
        {
            discard;
        }
    }
    
    // ライティングの判定と計算
    if (gMaterial.enableLighting != 0)
    {
        float3 N = normalize(input.normal);
        float3 L = normalize(-gDirectionalLight.direction);
        float NdotL = dot(N, L);
        
        float lightingFactor = 1.0f;
        if (gMaterial.lightingType == 1)
        {
            lightingFactor = saturate(NdotL);
        }
        else if (gMaterial.lightingType == 2)
        {
            lightingFactor = pow(NdotL * 0.5f + 0.5f, 2.0f);
        }
        
        output.color.rgb = baseColor.rgb * gDirectionalLight.color.rgb * lightingFactor * gDirectionalLight.intensity;
        output.color.a = alpha;
    }
    else
    {
        output.color.rgb = baseColor.rgb;
        output.color.a = alpha;
    }
    
    return output;
}