#include "Particle.hlsli"

StructuredBuffer<ParticleForGPU> gParticles : register(t0);

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;

    output.position = mul(input.position, gParticles[instanceId].wvp);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) gParticles[instanceId].world));
    output.color = gParticles[instanceId].color;

    return output;
}