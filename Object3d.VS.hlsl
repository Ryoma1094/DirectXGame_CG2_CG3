#include "Object3d.hlsli"

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b1);

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    // 💡WVP行列を用いて頂点座標をスクリーン空間へ変換
    output.position = mul(input.position, gTransformationMatrix.wvp);
    
    // 💡UV座標は一旦そのままピクセルシェーダーへ渡す（変形はピクセル単位で行うため）
    output.texcoord = input.texcoord;
    
    // 💡法線をワールド座標系へ変換（左上3x3のみ掛け合わせる）して正規化し、PSへ渡す
    output.normal = normalize(mul(input.normal, (float3x3) gTransformationMatrix.world));
    
    return output;
}