struct PerView {
    float4x4 viewProjection;
};

ParameterBlock<PerView> gViewParams;

struct AssembledVertex {
    float3 position : POSITION;
};

struct CoarseVertex {
    float3 worldPosition;
};

struct VertexStageOutput {
    CoarseVertex coarseVertex : CoarseVertex;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexStageOutput vertexMain(AssembledVertex assembledVertex) {
    VertexStageOutput output;

    float3 position = assembledVertex.position;

    output.coarseVertex.worldPosition = position;
    output.sv_position = mul(gViewParams.viewProjection, float4(position, 1.0));

    return output;
}

[shader("fragment")]
float4 fragmentMain(CoarseVertex coarseVertex: CoarseVertex) : SV_Target {
    return float4(1.0, 1.0, 1.0, 1.0);
}
