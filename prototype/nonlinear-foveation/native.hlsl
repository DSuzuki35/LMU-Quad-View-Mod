// Owned-scene D3D11 experiment. Geometry is clipped before reaching this shader.
cbuffer Fixture : register(b0) {
    float4 config;         // gaze.xy, strength, synthetic material work
    float4 dimensions;     // width, height, clipped triangle count, tiles per axis
    float4 quadDimensions; // outer width/height, focus width/height
    float4 focusRegion;    // clamped center.xy, fraction, unused
    float4 clipA[16];
    float4 clipB[16];
    float4 clipC[16];
    float4 uvA[16]; // xy = UV, z = surface ID
    float4 uvB[16];
    float4 uvC[16];
};

float asinhPositive(float x) { return log(x + sqrt(x * x + 1)); }
float radius(float2 p) {
    float2 d = p - config.xy;
    float2 edge =
        float2(d.x >= 0 ? 1 - config.x : 1 + config.x, d.y >= 0 ? 1 - config.y : 1 + config.y);
    float2 r = abs(d) / edge;
    return max(r.x, r.y);
}
float2 warp(float2 p, bool inverseMap) {
    float t = radius(p), k = config.z;
    if (k < .0001 || t < .000001)
        return p;
    float a = asinhPositive(k);
    float mapped = inverseMap ? sinh(a * t) / k : asinhPositive(k * t) / a;
    return config.xy + (p - config.xy) * (mapped / t);
}
Texture2D<float4> sourceColor : register(t0);
Texture2D<float4> focusColor : register(t1);
SamplerState sourceSampler : register(s0);
float4 ComposeQuadPS(float4 position : SV_Position) : SV_Target {
    uint physicalWidth, physicalHeight;
    sourceColor.GetDimensions(physicalWidth, physicalHeight);
    float2 size = float2(physicalWidth, physicalHeight), uv = position.xy / size;
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    float2 local = (ndc - focusRegion.xy) / focusRegion.z;
    if (all(abs(local) <= 1)) {
        float2 pixel = (local * float2(.5, -.5) + .5) * quadDimensions.zw;
        return focusColor.SampleLevel(sourceSampler,
                                      clamp(pixel, .5, quadDimensions.zw - .5) / size, 0);
    }
    return sourceColor.SampleLevel(
        sourceSampler, clamp(uv * quadDimensions.xy, .5, quadDimensions.xy - .5) / size, 0);
}
float4 ReconstructPS(float4 position : SV_Position) : SV_Target {
    uint physicalWidth, physicalHeight;
    sourceColor.GetDimensions(physicalWidth, physicalHeight);
    float2 outputSize = float2(physicalWidth, physicalHeight);
    float2 ndc = float2(2 * position.x / outputSize.x - 1, 1 - 2 * position.y / outputSize.y);
    float2 sourceNdc = warp(ndc, false);
    float2 sourcePixel = (sourceNdc * float2(.5, -.5) + .5) * dimensions.xy;
    // This probe uses a small logical viewport in a full-sized allocation.
    // Clamp to the logical source's texel centers, never the unused clear area.
    sourcePixel = clamp(sourcePixel, .5, dimensions.xy - .5);
    return sourceColor.SampleLevel(sourceSampler, sourcePixel / outputSize, 0);
}
float radialScale(float t) {
    float k = config.z;
    if (k < .0001)
        return 1;
    return t < .000001 ? k / asinhPositive(k) : asinhPositive(k * t) / (asinhPositive(k) * t);
}
float4 material(float2 uv, float id) {
    float3 color = float3(.15 + .75 * uv.x, .15 + .75 * uv.y, .2 + .3 * id);
    float detail = 0;
    [loop] for (int i = 0; i < (int)config.w; i++) {
        float f = i + 1;
        detail += sin(uv.x * f * 17) * cos(uv.y * f * 13) / max(1, config.w);
    }
    return float4(saturate(color * (1 + detail * .15)), id / 255);
}
float cross2(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }
float2 sampleNdc(float4 position) {
    // D3D image rows run top to bottom; eye-space Y runs bottom to top.
    return float2(2 * position.x / dimensions.x - 1, 1 - 2 * position.y / dimensions.y);
}

struct MeshVertex {
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
    float id : TEXCOORD1;
};
struct MeshOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float id : TEXCOORD1;
};
MeshOutput OrdinaryVS(MeshVertex v, uint instance : SV_InstanceID) {
    MeshOutput o;
    o.position = v.position;
    uint tiles = (uint)dimensions.w;
    float2 origin = -1 + (float2(instance % tiles, instance / tiles) + .5) * 2 / tiles;
    o.position.xy = v.position.xy / tiles + origin * v.position.w;
    o.uv = v.uv;
    o.id = v.id;
    return o;
}
MeshOutput WarpedVS(MeshVertex v, uint instance : SV_InstanceID) {
    MeshOutput o = OrdinaryVS(v, instance);
    o.position.xy = warp(o.position.xy / o.position.w, false) * o.position.w;
    return o;
}
float4 OrdinaryPS(MeshOutput v) : SV_Target { return material(v.uv, v.id); }
MeshOutput FocusVS(MeshVertex v, uint instance : SV_InstanceID) {
    MeshOutput o = OrdinaryVS(v, instance);
    o.position.xy = (o.position.xy - focusRegion.xy * o.position.w) / focusRegion.z;
    return o;
}

struct BoundOutput {
    float4 position : SV_Position;
    nointerpolation float4 a : TEXCOORD0;
    nointerpolation float4 b : TEXCOORD1;
    nointerpolation float4 c : TEXCOORD2;
    nointerpolation float2 ua : TEXCOORD3;
    nointerpolation float2 ub : TEXCOORD4;
    nointerpolation float2 uc : TEXCOORD5;
    nointerpolation float id : TEXCOORD6;
};
[maxvertexcount(4)] void BoundsGS(triangle MeshOutput input[3],
                                  inout TriangleStream<BoundOutput> stream) {
    float2 a = input[0].position.xy / input[0].position.w;
    float2 b = input[1].position.xy / input[1].position.w;
    float2 c = input[2].position.xy / input[2].position.w;
    float2 lo = min(a, min(b, c)), hi = max(a, max(b, c));
    float minimumRadius = radius(clamp(config.xy, lo, hi));
    float maximumRadius = max(max(radius(lo), radius(hi)),
                              max(radius(float2(lo.x, hi.y)), radius(float2(hi.x, lo.y))));
    // f(t)/t decreases monotonically. These interval products enclose every
    // warped point in the input rectangle, hence the whole input triangle.
    float lower = radialScale(maximumRadius), upper = radialScale(minimumRadius);
    float2 p0 = (lo - config.xy) * lower, p1 = (lo - config.xy) * upper;
    float2 p2 = (hi - config.xy) * lower, p3 = (hi - config.xy) * upper;
    float2 outLo = max(-1, config.xy + min(min(p0, p1), min(p2, p3)) - 2 / dimensions.xy);
    float2 outHi = min(1, config.xy + max(max(p0, p1), max(p2, p3)) + 2 / dimensions.xy);
    BoundOutput o;
    o.a = input[0].position;
    o.b = input[1].position;
    o.c = input[2].position;
    o.ua = input[0].uv;
    o.ub = input[1].uv;
    o.uc = input[2].uv;
    o.id = input[0].id;
    const float2 corners[4] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1)};
    [unroll] for (int i = 0; i < 4; i++) {
        o.position = float4(lerp(outLo, outHi, corners[i]), .5, 1);
        stream.Append(o);
    }
    stream.RestartStrip();
} struct PixelOutput {
    float4 color : SV_Target;
    float depth : SV_Depth;
};
PixelOutput BoundedPS(BoundOutput v) {
    float2 p = warp(sampleNdc(v.position), true);
    float2 a = v.a.xy / v.a.w, b = v.b.xy / v.b.w, c = v.c.xy / v.c.w;
    float determinant = cross2(b - a, c - a);
    if (abs(determinant) < 1e-8)
        discard;
    float lb = cross2(p - a, c - a) / determinant, lc = cross2(b - a, p - a) / determinant;
    float3 l = float3(1 - lb - lc, lb, lc);
    if (any(l < 0))
        discard;
    float z = dot(l, float3(v.a.z / v.a.w, v.b.z / v.b.w, v.c.z / v.c.w));
    if (z < 0 || z > 1)
        discard;
    float3 r = l / float3(v.a.w, v.b.w, v.c.w);
    float2 uv = (r.x * v.ua + r.y * v.ub + r.z * v.uc) / (r.x + r.y + r.z);
    PixelOutput o;
    o.color = material(uv, v.id);
    o.depth = z;
    return o;
}
float4 FullscreenVS(uint vertex : SV_VertexID) : SV_Position {
    float2 p = float2((vertex << 1) & 2, vertex & 2);
    return float4(p * 2 - 1, 0, 1);
}
PixelOutput AnalyticPS(float4 position : SV_Position) {
    float2 p = warp(sampleNdc(position), true);
    PixelOutput o;
    o.color = 0;
    o.depth = 1;
    [loop] for (int i = 0; i < (int)dimensions.z; i++) {
        float2 a = clipA[i].xy / clipA[i].w, b = clipB[i].xy / clipB[i].w,
               c = clipC[i].xy / clipC[i].w;
        float determinant = cross2(b - a, c - a);
        if (abs(determinant) < 1e-8)
            continue;
        float lb = cross2(p - a, c - a) / determinant, lc = cross2(b - a, p - a) / determinant;
        float3 l = float3(1 - lb - lc, lb, lc);
        if (any(l < 0))
            continue;
        float z = dot(
            l, float3(clipA[i].z / clipA[i].w, clipB[i].z / clipB[i].w, clipC[i].z / clipC[i].w));
        if (z < 0 || z > 1 || z >= o.depth)
            continue;
        float3 r = l / float3(clipA[i].w, clipB[i].w, clipC[i].w);
        float2 uv = (r.x * uvA[i].xy + r.y * uvB[i].xy + r.z * uvC[i].xy) / (r.x + r.y + r.z);
        o.color = material(uv, uvA[i].z);
        o.depth = z;
    }
    return o;
}
