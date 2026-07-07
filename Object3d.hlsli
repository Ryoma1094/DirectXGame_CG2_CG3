struct VertexShaderInput{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
};

struct VertexShaderOutput{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct Material{
    float4 color;
};

struct TransformationMatrix{
    float4x4 WVP;
};