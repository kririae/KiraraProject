struct PerView {
    float4x4 viewProjection;
};

struct PerModel {
    float4x4 modelMatrix;
    float4x4 inverseTransposedModelMatrix;
};

ParameterBlock<PerView> gViewParams;
ParameterBlock<PerModel> gModelParams;

struct AssembledVertex {
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct CoarseVertex {
    float3 worldPosition;
    float3 worldNormal;
};

struct Fragment {
    float4 color;
};

struct VertexStageOutput {
    CoarseVertex coarseVertex : CoarseVertex;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexStageOutput vertexMain(AssembledVertex assembledVertex) {
    VertexStageOutput output;

    float3 position = mul(gModelParams.modelMatrix, float4(assembledVertex.position, 1.0)).xyz;
    float3 normal = mul(gModelParams.inverseTransposedModelMatrix, float4(assembledVertex.normal, 0.0)).xyz;

    output.coarseVertex.worldPosition = position;
    output.coarseVertex.worldNormal = normal;
    output.sv_position = mul(gViewParams.viewProjection, float4(position, 1.0));

    return output;
}

[shader("fragment")]
float4 fragmentMain(CoarseVertex coarseVertex: CoarseVertex) : SV_Target {
    float3 color = saturate((coarseVertex.worldNormal + 1.0) / 2);
    return float4(color, 1.0);
}
