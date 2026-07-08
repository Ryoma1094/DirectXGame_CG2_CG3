struct VertexShaderInput{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VertexShaderOutput{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct Material{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
};

struct TransformationMatrix{
    float4x4 WVP;
    float4x4 World;
};

struct DirectionalLight{
    float4 color;
    float3 direction;
    float intensity;
};