// Standalone owned-scene probe. No OpenXR, injection, game files or hooks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {
// One fixture per process; selected once by main before any resources exist.
UINT width = 640, height = 480;
template <class T> struct Com {
    T *p = nullptr;
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com() {
        if (p)
            p->Release();
    }
    T *operator->() const { return p; }
};
void check(HRESULT hr, const char *operation) {
    if (FAILED(hr)) {
        std::ostringstream text;
        text << operation << " failed: 0x" << std::hex << static_cast<unsigned>(hr);
        throw std::runtime_error(text.str());
    }
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Float4 {
    float x = 0, y = 0, z = 0, w = 0;
};
struct Vertex {
    Float4 clip;
    float u = 0, v = 0, id = 0;
};
using Triangle = std::array<Vertex, 3>;
Vertex vertex(float x, float y, float z, float w, float u, float v, float id) {
    // Browser fixture uses OpenGL Z in [-1,1]; D3D clip Z is [0,W].
    return {{x * w, y * w, (z * .5f + .5f) * w, w}, u, v, id};
}
Vertex mix(const Vertex &a, const Vertex &b, float t) {
    const auto f = [t](float x, float y) { return x + (y - x) * t; };
    return {{f(a.clip.x, b.clip.x), f(a.clip.y, b.clip.y), f(a.clip.z, b.clip.z),
             f(a.clip.w, b.clip.w)},
            f(a.u, b.u),
            f(a.v, b.v),
            a.id};
}
float plane(const Vertex &v, int index) {
    switch (index) {
    case 0:
        return v.clip.w + v.clip.x;
    case 1:
        return v.clip.w - v.clip.x;
    case 2:
        return v.clip.w + v.clip.y;
    case 3:
        return v.clip.w - v.clip.y;
    case 4:
        return v.clip.z;
    default:
        return v.clip.w - v.clip.z;
    }
}
std::vector<Triangle> makeScene() {
    const std::array<Triangle, 2> original{
        {{{vertex(-.88f, -.72f, -.25f, 1, 0, 0, 1), vertex(.86f, -.55f, .3f, 2.8f, 1, 0, 1),
           vertex(-.08f, .88f, .15f, 1.5f, .4f, 1, 1)}},
         {{vertex(-1.15f, .52f, -1.35f, .7f, 0, 0, 2), vertex(.93f, .66f, .45f, 2, 1, 0, 2),
           vertex(.48f, -.86f, -.1f, 1.2f, .5f, 1, 2)}}}};
    std::vector<Triangle> triangles;
    for (const auto &triangle : original) {
        std::vector<Vertex> polygon(triangle.begin(), triangle.end());
        for (int side = 0; side < 6; side++) {
            std::vector<Vertex> clipped;
            for (size_t i = 0; i < polygon.size(); i++) {
                const auto &a = polygon[i];
                const auto &b = polygon[(i + 1) % polygon.size()];
                const float fa = plane(a, side), fb = plane(b, side);
                if (fa >= 0)
                    clipped.push_back(a);
                if ((fa >= 0) != (fb >= 0))
                    clipped.push_back(mix(a, b, fa / (fa - fb)));
            }
            polygon = std::move(clipped);
        }
        for (size_t i = 1; i + 1 < polygon.size(); i++)
            triangles.push_back({polygon[0], polygon[i], polygon[i + 1]});
    }
    require(triangles.size() == 3, "Owned fixture clipping changed unexpectedly");
    return triangles;
}
std::vector<Triangle> tiledScene(UINT tiles) {
    const auto original = makeScene();
    std::vector<Triangle> result;
    for (UINT y = 0; y < tiles; y++)
        for (UINT x = 0; x < tiles; x++)
            for (auto tri : original) {
                const float ox = -1 + (float(x) + .5f) * 2 / float(tiles),
                            oy = -1 + (float(y) + .5f) * 2 / float(tiles);
                for (auto &v : tri) {
                    v.clip.x = v.clip.x / float(tiles) + ox * v.clip.w;
                    v.clip.y = v.clip.y / float(tiles) + oy * v.clip.w;
                }
                result.push_back(tri);
            }
    return result;
}
struct alignas(16) Constants {
    Float4 config, dimensions;
    std::array<Float4, 16> a{}, b{}, c{}, ua{}, ub{}, uc{};
};
static_assert(sizeof(Float4) == 16 && sizeof(Vertex) == 28, "Shader input layout changed");
static_assert(sizeof(Constants) % 16 == 0, "D3D constant buffers require 16-byte size alignment");
struct Image {
    std::vector<uint8_t> rgba;
    std::vector<float> depth;
};
struct Metrics {
    unsigned wrong = 0, interiorWrong = 0, interior = 0;
    double rmse = 0, depthMax = 0;
    unsigned colorMax = 0;
};
Image cpuReference(const std::vector<Triangle> &triangles, float strength, float gx, float gy) {
    // Independent double-precision sample/visibility path. This does not use
    // shader code, the GS bounds or rasterized candidate coverage.
    Image image;
    image.rgba.resize(width * height * 4);
    image.depth.assign(width * height, 1);
    for (UINT y = 0; y < height; y++)
        for (UINT x = 0; x < width; x++) {
            double px = 2 * (double(x) + .5) / width - 1, py = 1 - 2 * (double(y) + .5) / height;
            const double dx = px - gx, dy = py - gy;
            const double t = std::max(std::abs(dx) / (dx >= 0 ? 1 - double(gx) : 1 + double(gx)),
                                      std::abs(dy) / (dy >= 0 ? 1 - double(gy) : 1 + double(gy)));
            if (strength > 0 && t > 0) {
                const double factor = std::sinh(std::asinh(double(strength)) * t) / (strength * t);
                px = gx + dx * factor;
                py = gy + dy * factor;
            }
            const size_t pixel = size_t(y) * width + x;
            double best = 1;
            for (const auto &tri : triangles) {
                const auto &a = tri[0];
                const auto &b = tri[1];
                const auto &c = tri[2];
                const double ax = double(a.clip.x) / a.clip.w, ay = double(a.clip.y) / a.clip.w;
                const double bx = double(b.clip.x) / b.clip.w, by = double(b.clip.y) / b.clip.w;
                const double cx = double(c.clip.x) / c.clip.w, cy = double(c.clip.y) / c.clip.w;
                const double determinant = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                if (std::abs(determinant) < 1e-12)
                    continue;
                const double lb = ((px - ax) * (cy - ay) - (py - ay) * (cx - ax)) / determinant;
                const double lc = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / determinant,
                             la = 1 - lb - lc;
                if (la < 0 || lb < 0 || lc < 0)
                    continue;
                const double z =
                    la * a.clip.z / a.clip.w + lb * b.clip.z / b.clip.w + lc * c.clip.z / c.clip.w;
                if (z < 0 || z >= best)
                    continue;
                const double ra = la / a.clip.w, rb = lb / b.clip.w, rc = lc / c.clip.w;
                const double u = (ra * a.u + rb * b.u + rc * c.u) / (ra + rb + rc),
                             v = (ra * a.v + rb * b.v + rc * c.v) / (ra + rb + rc);
                const std::array<double, 3> color{.15 + .75 * u, .15 + .75 * v, .2 + .3 * a.id};
                for (size_t channel = 0; channel < 3; channel++)
                    image.rgba[pixel * 4 + channel] =
                        static_cast<uint8_t>(std::lround(std::clamp(color[channel], 0., 1.) * 255));
                image.rgba[pixel * 4 + 3] = static_cast<uint8_t>(a.id);
                image.depth[pixel] = static_cast<float>(z);
                best = z;
            }
        }
    return image;
}
Metrics compare(const Image &candidate, const Image &reference) {
    Metrics result;
    double sum = 0;
    size_t channels = 0;
    for (UINT y = 0; y < height; y++)
        for (UINT x = 0; x < width; x++) {
            const size_t p = size_t(y) * width + x, i = p * 4;
            const auto id = reference.rgba[i + 3];
            const bool wrong = candidate.rgba[i + 3] != id;
            if (wrong)
                result.wrong++;
            if (x == 0 || y == 0 || x == width - 1 || y == height - 1)
                continue;
            if (reference.rgba[i - 4 + 3] != id || reference.rgba[i + 4 + 3] != id ||
                reference.rgba[i - width * 4 + 3] != id || reference.rgba[i + width * 4 + 3] != id)
                continue;
            result.interior++;
            if (wrong)
                result.interiorWrong++;
            else if (id) {
                for (size_t c = 0; c < 3; c++) {
                    const double delta = double(candidate.rgba[i + c]) - reference.rgba[i + c];
                    result.colorMax =
                        std::max(result.colorMax, static_cast<unsigned>(std::abs(delta)));
                    sum += delta * delta;
                    channels++;
                }
                result.depthMax = std::max(
                    result.depthMax, std::abs(double(candidate.depth[p]) - reference.depth[p]));
            }
        }
    result.rmse = std::sqrt(sum / std::max<size_t>(1, channels));
    return result;
}
void saveBmp(const std::filesystem::path &file, const Image &image) {
    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER info{};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(info);
    fileHeader.bfSize = fileHeader.bfOffBits + width * height * 4;
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = -static_cast<LONG>(height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    auto bgra = image.rgba;
    for (size_t i = 0; i < bgra.size(); i += 4) {
        std::swap(bgra[i], bgra[i + 2]);
        bgra[i + 3] = 255;
    }
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));
    out.write(reinterpret_cast<const char *>(&info), sizeof(info));
    out.write(reinterpret_cast<const char *>(bgra.data()),
              static_cast<std::streamsize>(bgra.size()));
    require(bool(out), "Could not export native image");
}
std::vector<uint8_t> cpuReconstruct(const Image &source, float strength, float gx, float gy,
                                    float scale) {
    const UINT sw = static_cast<UINT>(std::round(width * scale)),
               sh = static_cast<UINT>(std::round(height * scale));
    std::vector<uint8_t> result(width * height * 4);
    for (UINT y = 0; y < height; y++)
        for (UINT x = 0; x < width; x++) {
            double px = 2 * (double(x) + .5) / width - 1, py = 1 - 2 * (double(y) + .5) / height;
            const double dx = px - gx, dy = py - gy;
            const double t = std::max(std::abs(dx) / (dx >= 0 ? 1 - double(gx) : 1 + double(gx)),
                                      std::abs(dy) / (dy >= 0 ? 1 - double(gy) : 1 + double(gy)));
            if (strength > 0 && t > 0) {
                const double factor = std::asinh(strength * t) / (std::asinh(double(strength)) * t);
                px = gx + dx * factor;
                py = gy + dy * factor;
            }
            const double sx = std::clamp((px * .5 + .5) * sw - .5, 0., double(sw - 1));
            const double sy = std::clamp((.5 - py * .5) * sh - .5, 0., double(sh - 1));
            const UINT x0 = static_cast<UINT>(sx), y0 = static_cast<UINT>(sy),
                       x1 = std::min(x0 + 1, sw - 1), y1 = std::min(y0 + 1, sh - 1);
            const double fx = sx - x0, fy = sy - y0;
            for (size_t c = 0; c < 4; c++) {
                const auto sample = [&](UINT xx, UINT yy) {
                    return double(source.rgba[(size_t(yy) * width + xx) * 4 + c]);
                };
                const double top = sample(x0, y0) * (1 - fx) + sample(x1, y0) * fx;
                const double bottom = sample(x0, y1) * (1 - fx) + sample(x1, y1) * fx;
                result[(size_t(y) * width + x) * 4 + c] =
                    static_cast<uint8_t>(std::lround(top * (1 - fy) + bottom * fy));
            }
        }
    return result;
}
unsigned maxDifference(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    require(a.size() == b.size(), "Image size mismatch");
    unsigned maximum = 0;
    for (size_t i = 0; i < a.size(); i++)
        maximum = std::max(maximum, static_cast<unsigned>(std::abs(int(a[i]) - int(b[i]))));
    return maximum;
}
double rgbRmse(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    require(a.size() == b.size(), "Image size mismatch");
    double sum = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        for (size_t c = 0; c < 3; c++) {
            const double d = double(a[i + c]) - b[i + c];
            sum += d * d;
        }
    return std::sqrt(sum / (double(a.size()) * .75));
}
enum class Path { ordinary, vertices, bounded, analytic };
class Fixture {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<ID3D11VertexShader> ordinaryVS, warpedVS, fullscreenVS;
    Com<ID3D11GeometryShader> boundsGS;
    Com<ID3D11PixelShader> ordinaryPS, boundedPS, analyticPS, reconstructPS;
    Com<ID3D11Buffer> vertices, constants;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11Texture2D> color, depth, colorRead, depthRead, outputColor;
    Com<ID3D11RenderTargetView> rtv, outputRtv;
    Com<ID3D11ShaderResourceView> sourceView;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11DepthStencilView> dsv;
    Com<ID3D11RasterizerState> raster;
    Com<ID3D11DepthStencilState> depthState;
    Constants values{};
    UINT vertexCount = 0;
    std::string shaderSource;
    void compile(const char *entry, const char *profile, Com<ID3DBlob> &code) {
        Com<ID3DBlob> errors;
        const HRESULT hr =
            D3DCompile(shaderSource.data(), shaderSource.size(), "native.hlsl", nullptr, nullptr,
                       entry, profile,
                       D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
                           D3DCOMPILE_OPTIMIZATION_LEVEL3,
                       0, &code.p, &errors.p);
        if (FAILED(hr) && errors.p)
            std::cerr << static_cast<const char *>(errors->GetBufferPointer());
        check(hr, entry);
    }
    template <class T> T queryData(ID3D11Query *query) {
        T result{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        HRESULT hr;
        while ((hr = context->GetData(query, &result, sizeof(result), 0)) == S_FALSE) {
            require(std::chrono::steady_clock::now() < deadline, "Native query timed out");
            Sleep(1);
        }
        check(hr, "GetData");
        return result;
    }

  public:
    std::string adapter;
    Fixture(const std::filesystem::path &shader, bool hardware) {
        std::ifstream input(shader, std::ios::binary);
        require(bool(input), "Could not read native.hlsl");
        shaderSource = {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL wanted = D3D_FEATURE_LEVEL_11_0;
        check(D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,
                                nullptr, 0, &wanted, 1, D3D11_SDK_VERSION, &device.p, &level,
                                &context.p),
              "D3D11CreateDevice");
        Com<IDXGIDevice> dxgi;
        Com<IDXGIAdapter> graphics;
        check(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi.p)),
              "DXGI device");
        check(dxgi->GetAdapter(&graphics.p), "GetAdapter");
        DXGI_ADAPTER_DESC desc{};
        check(graphics->GetDesc(&desc), "GetDesc");
        for (const wchar_t c : desc.Description) {
            if (!c)
                break;
            adapter.push_back(c < 128 ? static_cast<char>(c) : '?');
        }
        for (const auto &entry : std::array<std::pair<const char *, Com<ID3D11VertexShader> *>, 3>{
                 {{"OrdinaryVS", &ordinaryVS},
                  {"WarpedVS", &warpedVS},
                  {"FullscreenVS", &fullscreenVS}}}) {
            Com<ID3DBlob> code;
            compile(entry.first, "vs_5_0", code);
            check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(),
                                             nullptr, &entry.second->p),
                  "CreateVertexShader");
            if (entry.second == &ordinaryVS) {
                const D3D11_INPUT_ELEMENT_DESC elements[]{
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
                     D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA,
                     0},
                    {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
                check(device->CreateInputLayout(elements, 3, code->GetBufferPointer(),
                                                code->GetBufferSize(), &layout.p),
                      "CreateInputLayout");
            }
        }
        for (const auto &entry : std::array<std::pair<const char *, Com<ID3D11PixelShader> *>, 4>{
                 {{"OrdinaryPS", &ordinaryPS},
                  {"BoundedPS", &boundedPS},
                  {"AnalyticPS", &analyticPS},
                  {"ReconstructPS", &reconstructPS}}}) {
            Com<ID3DBlob> code;
            compile(entry.first, "ps_5_0", code);
            check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
                                            nullptr, &entry.second->p),
                  "CreatePixelShader");
        }
        {
            Com<ID3DBlob> code;
            compile("BoundsGS", "gs_5_0", code);
            check(device->CreateGeometryShader(code->GetBufferPointer(), code->GetBufferSize(),
                                               nullptr, &boundsGS.p),
                  "CreateGeometryShader");
        }
        const auto triangles = makeScene();
        std::vector<Vertex> data;
        for (size_t i = 0; i < triangles.size(); i++) {
            const auto &t = triangles[i];
            data.insert(data.end(), t.begin(), t.end());
            values.a[i] = t[0].clip;
            values.b[i] = t[1].clip;
            values.c[i] = t[2].clip;
            values.ua[i] = {t[0].u, t[0].v, t[0].id, 0};
            values.ub[i] = {t[1].u, t[1].v, 0, 0};
            values.uc[i] = {t[2].u, t[2].v, 0, 0};
        }
        vertexCount = static_cast<UINT>(data.size());
        values.dimensions = {float(width), float(height), float(triangles.size()), 0};
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = static_cast<UINT>(data.size() * sizeof(Vertex));
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{data.data(), 0, 0};
        check(device->CreateBuffer(&buffer, &initial, &vertices.p), "Create vertices");
        buffer.ByteWidth = sizeof(Constants);
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&buffer, nullptr, &constants.p), "Create constants");
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = width;
        texture.Height = height;
        texture.MipLevels = 1;
        texture.ArraySize = 1;
        texture.SampleDesc.Count = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&texture, nullptr, &color.p), "Create color");
        check(device->CreateRenderTargetView(color.p, nullptr, &rtv.p), "Create RTV");
        check(device->CreateShaderResourceView(color.p, nullptr, &sourceView.p),
              "Create source SRV");
        texture.BindFlags = D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&texture, nullptr, &outputColor.p),
              "Create reconstructed color");
        check(device->CreateRenderTargetView(outputColor.p, nullptr, &outputRtv.p),
              "Create output RTV");
        D3D11_SAMPLER_DESC sampling{};
        sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampling.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampling.MaxAnisotropy = 1;
        sampling.MaxLOD = D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sampling, &sampler.p), "Create reconstruction sampler");
        texture.Format = DXGI_FORMAT_R32_TYPELESS;
        texture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        check(device->CreateTexture2D(&texture, nullptr, &depth.p), "Create depth");
        D3D11_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_D32_FLOAT;
        view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        check(device->CreateDepthStencilView(depth.p, &view, &dsv.p), "Create DSV");
        texture.BindFlags = 0;
        texture.Usage = D3D11_USAGE_STAGING;
        texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&texture, nullptr, &depthRead.p), "Create depth readback");
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        check(device->CreateTexture2D(&texture, nullptr, &colorRead.p), "Create color readback");
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&rs, &raster.p), "Create rasterizer");
        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_LESS;
        check(device->CreateDepthStencilState(&ds, &depthState.p), "Create depth state");
        configure(0, 0, 0);
    }
    ~Fixture() {
        if (context.p)
            context->ClearState();
    }
    void configure(float k, float gx, float gy, UINT tiles = 1, float work = 0, float scale = 1) {
        values.config = {gx, gy, k, work};
        values.dimensions.w = float(tiles);
        values.dimensions.x = std::max(1.f, std::round(width * scale));
        values.dimensions.y = std::max(1.f, std::round(height * scale));
        context->UpdateSubresource(constants.p, 0, nullptr, &values, 0, 0);
    }
    void draw(Path path) {
        const float clear[4]{};
        context->ClearRenderTargetView(rtv.p, clear);
        context->ClearDepthStencilView(dsv.p, D3D11_CLEAR_DEPTH, 1, 0);
        context->OMSetRenderTargets(1, &rtv.p, dsv.p);
        context->OMSetDepthStencilState(depthState.p, 0);
        context->RSSetState(raster.p);
        const D3D11_VIEWPORT viewport{0, 0, values.dimensions.x, values.dimensions.y, 0, 1};
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const UINT stride = sizeof(Vertex), offset = 0;
        context->IASetVertexBuffers(0, 1, &vertices.p, &stride, &offset);
        context->IASetInputLayout(path == Path::analytic ? nullptr : layout.p);
        context->VSSetConstantBuffers(0, 1, &constants.p);
        context->GSSetConstantBuffers(0, 1, &constants.p);
        context->PSSetConstantBuffers(0, 1, &constants.p);
        context->VSSetShader(path == Path::analytic   ? fullscreenVS.p
                             : path == Path::vertices ? warpedVS.p
                                                      : ordinaryVS.p,
                             nullptr, 0);
        context->GSSetShader(path == Path::bounded ? boundsGS.p : nullptr, nullptr, 0);
        context->PSSetShader(path == Path::analytic  ? analyticPS.p
                             : path == Path::bounded ? boundedPS.p
                                                     : ordinaryPS.p,
                             nullptr, 0);
        if (path == Path::analytic)
            context->Draw(3, 0);
        else {
            const UINT tiles = static_cast<UINT>(values.dimensions.w);
            context->DrawInstanced(vertexCount, tiles * tiles, 0, 0);
        }
    }
    void reconstruct() {
        context->OMSetRenderTargets(1, &outputRtv.p, nullptr);
        const D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->VSSetShader(fullscreenVS.p, nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->PSSetShader(reconstructPS.p, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &constants.p);
        context->PSSetShaderResources(0, 1, &sourceView.p);
        context->PSSetSamplers(0, 1, &sampler.p);
        context->Draw(3, 0);
        ID3D11ShaderResourceView *empty = nullptr;
        context->PSSetShaderResources(0, 1, &empty);
    }
    std::vector<uint8_t> readReconstruction() {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(colorRead.p, outputColor.p);
        std::vector<uint8_t> result(width * height * 4);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(colorRead.p, 0, D3D11_MAP_READ, 0, &mapped), "Map reconstruction");
        for (UINT y = 0; y < height; y++)
            std::memcpy(result.data() + size_t(y) * width * 4,
                        static_cast<uint8_t *>(mapped.pData) + size_t(y) * mapped.RowPitch,
                        width * 4);
        context->Unmap(colorRead.p, 0);
        return result;
    }
    Image read() {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(colorRead.p, color.p);
        context->CopyResource(depthRead.p, depth.p);
        Image image;
        image.rgba.resize(width * height * 4);
        image.depth.resize(width * height);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(colorRead.p, 0, D3D11_MAP_READ, 0, &mapped), "Map color");
        for (UINT y = 0; y < height; y++)
            std::memcpy(image.rgba.data() + size_t(y) * width * 4,
                        static_cast<uint8_t *>(mapped.pData) + size_t(y) * mapped.RowPitch,
                        width * 4);
        context->Unmap(colorRead.p, 0);
        check(context->Map(depthRead.p, 0, D3D11_MAP_READ, 0, &mapped), "Map depth");
        for (UINT y = 0; y < height; y++)
            std::memcpy(image.depth.data() + size_t(y) * width,
                        static_cast<uint8_t *>(mapped.pData) + size_t(y) * mapped.RowPitch,
                        width * 4);
        context->Unmap(depthRead.p, 0);
        require(std::all_of(image.depth.begin(), image.depth.end(),
                            [](float z) { return std::isfinite(z) && z >= 0 && z <= 1; }),
                "Readback contains invalid original depth");
        return image;
    }
    D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics(Path path) {
        Com<ID3D11Query> query;
        const D3D11_QUERY_DESC desc{D3D11_QUERY_PIPELINE_STATISTICS, 0};
        check(device->CreateQuery(&desc, &query.p), "Create statistics");
        context->Begin(query.p);
        draw(path);
        context->End(query.p);
        context->Flush();
        return queryData<D3D11_QUERY_DATA_PIPELINE_STATISTICS>(query.p);
    }
    double time(Path path, bool complete = false) {
        Com<ID3D11Query> disjoint, start, finish;
        D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        check(device->CreateQuery(&desc, &disjoint.p), "Create disjoint query");
        desc.Query = D3D11_QUERY_TIMESTAMP;
        check(device->CreateQuery(&desc, &start.p), "Create start query");
        check(device->CreateQuery(&desc, &finish.p), "Create finish query");
        context->Begin(disjoint.p);
        context->End(start.p);
        for (int repeat = 0; repeat < 32; repeat++) {
            draw(path);
            if (complete)
                reconstruct();
        }
        context->End(finish.p);
        context->End(disjoint.p);
        context->Flush();
        const auto clock = queryData<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(disjoint.p);
        require(!clock.Disjoint && clock.Frequency > 0, "Disjoint native GPU clock");
        const auto first = queryData<UINT64>(start.p), last = queryData<UINT64>(finish.p);
        return double(last - first) * 1000 / double(clock.Frequency) / 32;
    }
};
void printMetrics(const Metrics &m) {
    std::cout << "\"wrongPixels\":" << m.wrong << ",\"interiorWrong\":" << m.interiorWrong
              << ",\"interiorPixels\":" << m.interior << ",\"colorRmse255\":" << m.rmse
              << ",\"maxDepthError\":" << m.depthMax << ",\"maxColorError255\":" << m.colorMax;
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc >= 3, "Usage: native.exe shader.hlsl output-directory [--hardware]");
        bool hardware = false, large = false;
        for (int i = 3; i < argc; i++) {
            const std::string argument = argv[i];
            if (argument == "--hardware")
                hardware = true;
            else if (argument == "--large")
                large = true;
            else
                throw std::runtime_error("Unknown native probe argument: " + argument);
        }
        require(!large || hardware, "Large preset is hardware-only");
        if (large) {
            width = 2048;
            height = 1536;
        }
        const std::filesystem::path output = argv[2];
        std::filesystem::create_directories(output);
        Fixture fixture(argv[1], hardware);
        std::cout << std::setprecision(10) << "{\"adapter\":\"" << fixture.adapter
                  << "\",\"hardware\":" << (hardware ? "true" : "false") << ",\"width\":" << width
                  << ",\"height\":" << height << ",\"cases\":[\n";
        bool comma = false;
        const auto triangles = makeScene();
        for (const float k : {0.f, .1f, 4.f, 12.f})
            for (const auto gaze :
                 std::array<std::array<float, 2>, 3>{{{0, 0}, {.8f, -.8f}, {-.7f, .6f}}}) {
                fixture.configure(k, gaze[0], gaze[1]);
                fixture.draw(Path::analytic);
                const auto reference = fixture.read();
                fixture.draw(Path::bounded);
                const auto bounded = fixture.read();
                const auto metrics = compare(bounded, reference);
                require(metrics.interiorWrong <= 2 && metrics.rmse < .1 &&
                            metrics.depthMax < .00001,
                        "Bounded native path failed coverage/color/depth gate");
                const auto independent = cpuReference(triangles, k, gaze[0], gaze[1]);
                const auto cpu = compare(bounded, independent);
                // A double CPU calculation and a native UNORM8 write may land
                // on opposite sides of a quantization boundary. Gate the MAX
                // difference at one code value instead of assuming byte identity.
                if (cpu.interiorWrong > 2 || cpu.colorMax > 1 || cpu.depthMax >= .00001)
                    std::cerr << "CPU witness k=" << k << " gaze=" << gaze[0] << ',' << gaze[1]
                              << " wrong=" << cpu.interiorWrong << " color=" << cpu.rmse
                              << " max=" << cpu.colorMax << " depth=" << cpu.depthMax << '\n';
                require(cpu.interiorWrong <= 2 && cpu.colorMax <= 1 && cpu.depthMax < .00001,
                        "Native bounded path differs from independent CPU reference");
                if (comma)
                    std::cout << ",\n";
                comma = true;
                std::cout << "{\"strength\":" << k << ",\"gaze\":[" << gaze[0] << ',' << gaze[1]
                          << "],";
                printMetrics(metrics);
                std::cout << ",\"cpuReference\":{";
                printMetrics(cpu);
                std::cout << '}';
                if (k == 0) {
                    fixture.draw(Path::bounded);
                    fixture.reconstruct();
                    require(fixture.readReconstruction() == bounded.rgba,
                            "Identity reconstruction is not byte-exact");
                    fixture.draw(Path::ordinary);
                    const auto ordinary = fixture.read();
                    const auto identity = compare(ordinary, reference);
                    require(identity.interiorWrong <= 2 && identity.rmse < .1 &&
                                identity.depthMax < .00001,
                            "Native analytic reference failed ordinary identity gate");
                    std::cout << ",\"ordinaryIdentity\":{";
                    printMetrics(identity);
                    std::cout << '}';
                }
                if (k == 4 && gaze[0] == 0) {
                    fixture.draw(Path::vertices);
                    const auto naive = fixture.read();
                    const auto errors = compare(naive, reference);
                    require(errors.interiorWrong > 1000,
                            "Native negative control did not expose vertex-warp errors");
                    std::cout << ",\"naiveNegativeControl\":{";
                    printMetrics(errors);
                    std::cout << '}';
                    const std::string prefix = hardware ? "hardware-" : "warp-";
                    saveBmp(output / (prefix + "bounded.bmp"), bounded);
                    saveBmp(output / (prefix + "reference.bmp"), reference);
                    saveBmp(output / (prefix + "naive.bmp"), naive);
                }
                std::cout << '}';
            }
        std::cout << "],\"reconstruction\":[";
        bool reconstructionComma = false;
        for (const auto gaze :
             std::array<std::array<float, 2>, 3>{{{0, 0}, {.8f, -.8f}, {-.7f, .6f}}}) {
            fixture.configure(4, gaze[0], gaze[1], 1, 0, .55f);
            fixture.draw(Path::bounded);
            const auto source = fixture.read();
            fixture.reconstruct();
            const auto reconstructed = fixture.readReconstruction();
            const unsigned maximum =
                maxDifference(reconstructed, cpuReconstruct(source, 4, gaze[0], gaze[1], .55f));
            require(maximum <= 2, "Native reconstruction differs from CPU bilinear reference");
            const double error = rgbRmse(reconstructed, cpuReference(triangles, 0, 0, 0).rgba);
            if (reconstructionComma)
                std::cout << ',';
            reconstructionComma = true;
            std::cout << "{\"scale\":0.55,\"gaze\":[" << gaze[0] << ',' << gaze[1]
                      << "],\"cpuMaximumCodeDifference\":" << maximum
                      << ",\"ordinaryColorRmse255\":" << error << '}';
            if (hardware && gaze[0] == 0) {
                saveBmp(output / "hardware-reconstructed-55.bmp", Image{reconstructed, {}});
                fixture.configure(0, 0, 0);
                fixture.draw(Path::ordinary);
                saveBmp(output / "hardware-ordinary.bmp", fixture.read());
            }
        }
        fixture.configure(4, 0, 0, 4);
        const auto tiledStats = fixture.statistics(Path::bounded);
        require(tiledStats.GSInvocations == 48 && tiledStats.GSPrimitives == 96,
                "Tiled probe did not emit the expected primitive count");
        std::cout << "],\"tiledCorrectness\":[";
        const auto tiled = tiledScene(4);
        bool tileComma = false;
        for (const float k : {0.f, 4.f, 12.f}) {
            fixture.configure(k, .8f, -.8f, 4);
            fixture.draw(Path::bounded);
            const auto image = fixture.read();
            const auto cpu = compare(image, cpuReference(tiled, k, .8f, -.8f));
            require(cpu.interiorWrong <= 2 && cpu.colorMax <= 1 && cpu.depthMax < .00001,
                    "Tiled nonlinear rendering differs from CPU reference");
            if (tileComma)
                std::cout << ',';
            tileComma = true;
            std::cout << "{\"strength\":" << k << ",\"tiles\":4,";
            printMetrics(cpu);
            std::cout << '}';
            if (k == 0) {
                fixture.draw(Path::ordinary);
                const auto identity = compare(image, fixture.read());
                require(identity.interiorWrong <= 2 && identity.colorMax <= 1 &&
                            identity.depthMax < .00001,
                        "Tiled native identity differs");
            }
            if (k == 4 && hardware)
                saveBmp(output / "hardware-tiled-bounded.bmp", image);
        }
        std::cout << "],\"pipeline\":[";
        fixture.configure(4, 0, 0);
        const std::array<Path, 4> paths{Path::ordinary, Path::vertices, Path::bounded,
                                        Path::analytic};
        const std::array<const char *, 4> names{"ordinary", "warpedVertices", "boundedGS",
                                                "analytic"};
        std::array<std::vector<double>, 4> times;
        for (int iteration = 0; iteration < 11; iteration++)
            for (size_t j = 0; j < paths.size(); j++) {
                const size_t index = (static_cast<size_t>(iteration) + j) % paths.size();
                const auto ms = fixture.time(paths[index]);
                if (iteration >= 2)
                    times[index].push_back(ms);
            }
        for (size_t i = 0; i < paths.size(); i++) {
            auto &samples = times[i];
            std::sort(samples.begin(), samples.end());
            const auto stats = fixture.statistics(paths[i]);
            if (paths[i] == Path::bounded)
                require(stats.GSInvocations == 3 && stats.GSPrimitives == 6,
                        "Geometry shader primitive counts differ from expected");
            if (i)
                std::cout << ',';
            std::cout << "{\"path\":\"" << names[i] << "\",\"medianMs\":" << samples[4]
                      << ",\"minMs\":" << samples.front() << ",\"maxMs\":" << samples.back()
                      << ",\"gsInvocations\":" << stats.GSInvocations
                      << ",\"gsPrimitives\":" << stats.GSPrimitives
                      << ",\"psInvocations\":" << stats.PSInvocations << '}';
        }
        std::cout << "],\"scaling\":[";
        if (hardware) {
            bool scalingComma = false;
            for (const UINT tiles : {1u, 4u, 16u, 64u})
                for (const float work : {0.f, 32.f}) {
                    fixture.configure(4, 0, 0, tiles, work);
                    std::array<std::vector<double>, 3> measured;
                    for (int iteration = 0; iteration < 11; iteration++)
                        for (size_t j = 0; j < 3; j++) {
                            const size_t index = (static_cast<size_t>(iteration) + j) % 3;
                            const double ms = fixture.time(paths[index]);
                            if (iteration >= 2)
                                measured[index].push_back(ms);
                        }
                    if (scalingComma)
                        std::cout << ',';
                    scalingComma = true;
                    std::cout << "{\"tilesPerAxis\":" << tiles
                              << ",\"inputTriangles\":" << 3 * tiles * tiles
                              << ",\"materialWork\":" << work << ",\"paths\":[";
                    for (size_t i = 0; i < 3; i++) {
                        auto &samples = measured[i];
                        std::sort(samples.begin(), samples.end());
                        const auto stats = fixture.statistics(paths[i]);
                        if (paths[i] == Path::bounded)
                            require(stats.GSInvocations == 3 * tiles * tiles &&
                                        stats.GSPrimitives == 6 * tiles * tiles,
                                    "Scaling GS counts differ");
                        if (i)
                            std::cout << ',';
                        std::cout << "{\"path\":\"" << names[i] << "\",\"medianMs\":" << samples[4]
                                  << ",\"minMs\":" << samples.front()
                                  << ",\"maxMs\":" << samples.back()
                                  << ",\"gsInvocations\":" << stats.GSInvocations
                                  << ",\"gsPrimitives\":" << stats.GSPrimitives
                                  << ",\"psInvocations\":" << stats.PSInvocations << '}';
                    }
                    std::cout << "]}";
                }
        }
        std::cout << "],\"completePaths\":[";
        if (hardware) {
            bool completeComma = false;
            for (const UINT tiles : {1u, 16u, 64u})
                for (const float work : {0.f, 32.f, 128.f}) {
                    std::array<std::vector<double>, 3> measured;
                    for (int iteration = 0; iteration < 11; iteration++)
                        for (size_t j = 0; j < 3; j++) {
                            const size_t index = (static_cast<size_t>(iteration) + j) % 3;
                            fixture.configure(4, 0, 0, tiles, work, index == 2 ? .55f : 1.f);
                            const double ms = fixture.time(
                                index == 0 ? Path::ordinary : Path::bounded, index != 0);
                            if (iteration >= 2)
                                measured[index].push_back(ms);
                        }
                    if (completeComma)
                        std::cout << ',';
                    completeComma = true;
                    std::cout << "{\"inputTriangles\":" << 3 * tiles * tiles
                              << ",\"materialWork\":" << work << ",\"paths\":[";
                    const std::array<const char *, 3> labels{
                        "ordinary", "boundedFullPlusReconstruction", "bounded55PlusReconstruction"};
                    for (size_t i = 0; i < 3; i++) {
                        auto &samples = measured[i];
                        std::sort(samples.begin(), samples.end());
                        if (i)
                            std::cout << ',';
                        std::cout << "{\"path\":\"" << labels[i] << "\",\"medianMs\":" << samples[4]
                                  << ",\"minMs\":" << samples.front()
                                  << ",\"maxMs\":" << samples.back() << '}';
                    }
                    std::cout << "]}";
                }
        }
        std::cout << "],\"timingScope\":\"pipeline/scaling: source only; completePaths: source "
                     "plus color reconstruction. Source allocations stay full size. One instanced "
                     "draw per path, no engine traversal. 2 warmups, 9 rotated "
                     "batches of 32. CPU creation/upload/readback excluded.\",\"passed\":true}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
