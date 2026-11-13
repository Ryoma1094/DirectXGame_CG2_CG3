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

// C++側のメモリ配置（16バイトアライメント）に合わせた構造体
struct Material
{
    float4 color; // 16バイト (offset: 0)
    int enableLighting; // 4バイト  (offset: 16)
    int lightingType; // 4バイト  (offset: 20)
    int enableAlphaDiscard; // 4バイト  (offset: 24)
    float discardThreshold; // 4バイト  (offset: 28)
    float4x4 uvTransform; // 64バイト (offset: 32)
};

// ★更新：wvp と world に加えて color を持つ構造体
struct ParticleForGPU
{
    float4x4 wvp;
    float4x4 world;
    float4 color;
};