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
    float4 color;
    int enableLighting; // 0: 無効, 1: 有効
    int lightingType; // 0: None, 1: Lambert, 2: Half Lambert
    float2 padding; // パディング（16バイト境界合わせ）
    float4x4 uvTransform;
};

struct TransformationMatrix
{
    float4x4 wvp;
    float4x4 world;
};