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
    float4 color : COLOR0;
};

// C++側の Struct 配置に合わせた構造体
struct Material
{
    float4 color;
    int enableLighting;
    int lightingType;
    int enableAlphaDiscard;
    float discardThreshold;
    float4x4 uvTransform;
};

// インスタンスデータ構造体 (StructuredBuffer用)
struct ParticleForGPU
{
    float4x4 wvp;
    float4x4 world;
    float4 color;
};