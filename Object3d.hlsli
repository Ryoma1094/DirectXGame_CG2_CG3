struct VertexShaderInput
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

// C++側のメモリ配置（16バイトアライメント）に合わせた構造体
struct Material
{
    float4 color; // 16バイト (offset: 0)
    int enableLighting; // 4バイト  (offset: 16)
    int lightingType; // 4バイト  (offset: 20)
    int enableAlphaDiscard; // 4バイト  (offset: 24) 0:無効, 1:有効 (2値抜き)
    float discardThreshold; // 4バイト  (offset: 28) 棄却アルファ閾値
    float4x4 uvTransform; // 64バイト (offset: 32)
};

struct TransformationMatrix
{
    float4x4 wvp;
    float4x4 world;
};