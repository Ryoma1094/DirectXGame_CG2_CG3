#include "Particle.hlsli"

// StructuredBufferでインスタンスごとのデータを受け取る (register t1)
StructuredBuffer<ParticleForGPU> gParticles : register(t1);

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    
    // インスタンスごとの WVP / World 行列を使用して変換
    output.position = mul(input.position, gParticles[instanceId].wvp);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) gParticles[instanceId].world));
    output.color = gParticles[instanceId].color;
    
    return output;
}