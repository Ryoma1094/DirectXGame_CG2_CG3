#include "Object3d.hlsli"

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b1);

VertexShaderOutput main(VertexShaderInput input){
    VertexShaderOutput output;
    
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    
    return output;
}