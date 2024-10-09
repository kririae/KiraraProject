cbuffer Uniforms { float4x4 modelViewProjection; }

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

    float3 position = assembledVertex.position;
    float3 normal = assembledVertex.normal;

    output.coarseVertex.worldPosition = position;
    output.coarseVertex.worldNormal = normal;
    output.sv_position = mul(modelViewProjection, float4(position, 1.0));

    return output;
}

[shader("fragment")]
float4 fragmentMain(CoarseVertex coarseVertex: CoarseVertex) : SV_Target {
    float3 color = saturate((coarseVertex.worldNormal + 1.0) / 2);
    return float4(color, 1.0);
}


