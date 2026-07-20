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
    
    // 💡【スライドの肝】入力されたテクスチャ座標(u, v)を同次座標 (u, v, 0, 1) にして
    // C++から送られてきた 4x4の uvTransform 行列を掛け合わせる
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    
    // 💡変換された新しいUV座標 (.xy) を使用してテクスチャをサンプリングする
    // 元のコードにあった赤青反転対策の .bgra もそのまま維持しています
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy).bgra;
    
    // ライティングを行うかどうかの条件分岐
    if (gMaterial.enableLighting != 0)
    {
        // 法線とライトの向きをしっかり正規化して内積をとる
        float NdotL = dot(normalize(input.normal), -normalize(gDirectionalLight.direction));
        
        // ハーフランバート照明の計算
        float halfLambert = pow(NdotL * 0.5f + 0.5f, 2.0f);
        
        // 各要素を乗算して最終カラーを決定
        output.color = gMaterial.color * textureColor * gDirectionalLight.color * halfLambert * gDirectionalLight.intensity;
    }
    else
    {
        // ライティング無効（スプライトなど）時は、マテリアル色とテクスチャ色をそのまま乗算
        output.color = gMaterial.color * textureColor;
    }
    
    return output;
}