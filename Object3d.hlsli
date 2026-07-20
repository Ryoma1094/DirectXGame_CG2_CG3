struct VertexShaderInput
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // 💡C++側と合わせるために法線を追加
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // 💡VSからPSへ渡すために法線を追加
};


// 💡スライド「Alignmentを考慮して書き換え」に対応
struct Material
{
    float4 color;
    int enableLighting;
    // HLSL側では int(4バイト) の後ろに float4x4(64バイト) が来ると
    // 自動的に12バイトのパディングが挟まれます。これでC++側とアライメントが完全一致します
    float4x4 uvTransform;
};

struct TransformationMatrix
{
    float4x4 wvp;
    float4x4 world; // 💡ワールド行列を追加
};