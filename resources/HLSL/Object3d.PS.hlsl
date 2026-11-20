#include "Object3d.hlsli"

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

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
    
    // ライティングの判定と計算
    if (gMaterial.enableLighting != 0)
    {
        float3 N = normalize(input.normal);
        float3 L = normalize(-gDirectionalLight.direction);
        float NdotL = dot(N, L);
        
        float lightingFactor = 1.0f;
        
        // 0: None, 1: Lambert, 2: Half Lambert
        if (gMaterial.lightingType == 1)
        {
            // Lambertian Reflectance
            lightingFactor = saturate(NdotL);
        }
        else if (gMaterial.lightingType == 2)
        {
            // Half Lambert
            lightingFactor = pow(NdotL * 0.5f + 0.5f, 2.0f);
        }
        
        output.color = gMaterial.color * textureColor * gDirectionalLight.color * lightingFactor * gDirectionalLight.intensity;
    }
    else
    {
        // ライティング無効（None）
        output.color = gMaterial.color * textureColor;
    }
    
    return output;
}