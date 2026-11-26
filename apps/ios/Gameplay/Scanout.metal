#include <metal_stdlib>
using namespace metal;
struct Vertex { float4 position [[position]]; float2 uv; };
vertex Vertex scanoutVertex(uint id [[vertex_id]]) {
    float2 uv = float2((id << 1) & 2, id & 2);
    return {float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1), uv};
}
fragment float4 scanoutPixel(Vertex in [[stage_in]], texture2d<float> frame [[texture(0)]]) {
    constexpr sampler sampleFrame(filter::nearest, address::clamp_to_edge);
    return frame.sample(sampleFrame, in.uv);
}

