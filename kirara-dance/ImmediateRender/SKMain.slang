struct PerView {
    float4x4 viewProjection;
};

ParameterBlock<PerView> gViewParams;

struct AssembledVertex {
    float3 position : POSITION;
    uint vertexID : SV_VertexID;
};

struct CoarseVertex {
    float3 worldPosition;
    uint vertexID;
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
    output.coarseVertex.vertexID = assembledVertex.vertexID;
    output.sv_position = mul(gViewParams.viewProjection, float4(position, 1.0));

    return output;
}

[shader("fragment")]
float4 fragmentMain(CoarseVertex coarseVertex: CoarseVertex) : SV_Target {
    uint lineSegmentID = coarseVertex.vertexID / 2;
    
    float r = frac(float(lineSegmentID) * 0.157);
    float g = frac(float(lineSegmentID) * 0.264);
    float b = frac(float(lineSegmentID) * 0.372);
    
    return float4(r, g, b, 1.0);
}
