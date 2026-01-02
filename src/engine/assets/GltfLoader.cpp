
#include "GltfLoader.hpp"

#include "engine/assets/mini_json.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {

static bool readFileText(const std::string& path, std::string& out, std::string& err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "Failed to open: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool readFileBin(const std::string& path, std::vector<uint8_t>& out, std::string& err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "Failed to open: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    size_t sz = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize(sz);
    f.read((char*)out.data(), (std::streamsize)sz);
    return true;
}

static std::string dirOf(const std::string& path)
{
    auto p = path.find_last_of("/\\");
    if (p == std::string::npos)
        return "";
    return path.substr(0, p + 1);
}

static uint32_t u32(const mini_json::Value& v, uint32_t def = 0)
{
    if (!v.is_num())
        return def;
    double d = v.as_num();
    if (d < 0)
        return def;
    return (uint32_t)d;
}
static int i32(const mini_json::Value& v, int def = -1)
{
    if (!v.is_num())
        return def;
    return (int)v.as_num();
}
static float f32(const mini_json::Value& v, float def = 0.0f)
{
    if (!v.is_num())
        return def;
    return (float)v.as_num();
}

static glm::mat4 nodeLocalMatrix(const mini_json::Value& node)
{
    if (auto* m = node.get("matrix"); m && m->is_arr() && m->as_arr().size() == 16) {
        glm::mat4 M(1.0f);
        for (int i = 0; i < 16; i++) {
            M[i / 4][i % 4] = f32(*m->at(i));
        }
        return M;
    }
    glm::vec3 T(0.0f);
    glm::vec3 S(1.0f);
    glm::quat R(1.0f, 0.0f, 0.0f, 0.0f);

    if (auto* t = node.get("translation"); t && t->is_arr() && t->as_arr().size() == 3) {
        T = { f32(*t->at(0)), f32(*t->at(1)), f32(*t->at(2)) };
    }
    if (auto* s = node.get("scale"); s && s->is_arr() && s->as_arr().size() == 3) {
        S = { f32(*s->at(0), 1), f32(*s->at(1), 1), f32(*s->at(2), 1) };
    }
    if (auto* r = node.get("rotation"); r && r->is_arr() && r->as_arr().size() == 4) {
        R = glm::quat(f32(*r->at(3), 1), f32(*r->at(0)), f32(*r->at(1)), f32(*r->at(2)));
    }

    glm::mat4 M = glm::translate(glm::mat4(1.0f), T) * glm::mat4_cast(R) * glm::scale(glm::mat4(1.0f), S);
    return M;
}

struct BufferView {
    int buffer = 0;
    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t byteStride = 0;
};
struct Accessor {
    int bufferView = -1;
    size_t byteOffset = 0;
    uint32_t componentType = 0;
    uint32_t count = 0;
    std::string type;
    bool normalized = false;
};

static size_t componentSize(uint32_t ct)
{
    switch (ct) {
        case 5120:
            return 1;
        case 5121:
            return 1;
        case 5122:
            return 2;
        case 5123:
            return 2;
        case 5125:
            return 4;
        case 5126:
            return 4;
        default:
            return 0;
    }
}
static size_t typeCount(const std::string& t)
{
    if (t == "SCALAR")
        return 1;
    if (t == "VEC2")
        return 2;
    if (t == "VEC3")
        return 3;
    if (t == "VEC4")
        return 4;
    if (t == "MAT4")
        return 16;
    return 0;
}

template <typename T>
static T readLE(const uint8_t* p)
{
    T v{};
    std::memcpy(&v, p, sizeof(T));
    return v;
}

static bool loadArrays(const mini_json::Value& root,
                       std::vector<BufferView>& bvs,
                       std::vector<Accessor>& accs,
                       std::vector<mini_json::Value> const*& meshes,
                       std::vector<mini_json::Value> const*& nodes,
                       std::vector<mini_json::Value> const*& scenes,
                       std::string& err)
{
    auto* bufferViews = root.get("bufferViews");
    auto* accessors = root.get("accessors");
    auto* meshesV = root.get("meshes");
    auto* nodesV = root.get("nodes");
    auto* scenesV = root.get("scenes");
    if (!bufferViews || !bufferViews->is_arr()) {
        err = "No bufferViews";
        return false;
    }
    if (!accessors || !accessors->is_arr()) {
        err = "No accessors";
        return false;
    }
    if (!meshesV || !meshesV->is_arr()) {
        err = "No meshes";
        return false;
    }
    if (!nodesV || !nodesV->is_arr()) {
        err = "No nodes";
        return false;
    }
    if (!scenesV || !scenesV->is_arr()) {
        err = "No scenes";
        return false;
    }

    bvs.resize(bufferViews->as_arr().size());
    for (size_t i = 0; i < bvs.size(); i++) {
        if (!bufferViews->as_arr()[i].is_obj()) {
            err = "bufferViews[" + std::to_string(i) + "] not an object";
            return false;
        }
        auto& o = bufferViews->as_arr()[i].as_obj();
        if (auto it = o.find("buffer"); it != o.end())
            bvs[i].buffer = i32(it->second, 0);
        else
            bvs[i].buffer = 0;
        if (auto it = o.find("byteOffset"); it != o.end())
            bvs[i].byteOffset = (size_t)u32(it->second, 0);
        if (auto it = o.find("byteLength"); it != o.end())
            bvs[i].byteLength = (size_t)u32(it->second, 0);
        else {
            err = "bufferViews[" + std::to_string(i) + "] missing byteLength";
            return false;
        }
        if (auto it = o.find("byteStride"); it != o.end())
            bvs[i].byteStride = (size_t)u32(it->second, 0);
    }

    accs.resize(accessors->as_arr().size());
    for (size_t i = 0; i < accs.size(); i++) {
        if (!accessors->as_arr()[i].is_obj()) {
            err = "accessors[" + std::to_string(i) + "] not an object";
            return false;
        }
        auto& o = accessors->as_arr()[i].as_obj();

        if (auto it = o.find("bufferView"); it != o.end())
            accs[i].bufferView = i32(it->second, -1);
        else
            accs[i].bufferView = -1;
        if (auto it = o.find("byteOffset"); it != o.end())
            accs[i].byteOffset = (size_t)u32(it->second, 0);
        if (auto it = o.find("componentType"); it != o.end())
            accs[i].componentType = u32(it->second, 0);
        else {
            err = "accessors[" + std::to_string(i) + "] missing componentType";
            return false;
        }
        if (auto it = o.find("count"); it != o.end())
            accs[i].count = u32(it->second, 0);
        else {
            err = "accessors[" + std::to_string(i) + "] missing count";
            return false;
        }
        if (auto it = o.find("type"); it != o.end() && it->second.is_str())
            accs[i].type = it->second.as_str();
        if (auto it = o.find("normalized"); it != o.end() && it->second.is_bool())
            accs[i].normalized = it->second.as_bool();
    }

    meshes = &meshesV->as_arr();
    nodes = &nodesV->as_arr();
    scenes = &scenesV->as_arr();
    return true;
}

static bool readAccessorVec(const std::vector<uint8_t>& bin,
                            const std::vector<BufferView>& bvs,
                            const Accessor& a,
                            std::vector<float>& out,
                            size_t comps,
                            std::string& err)
{
    if (a.bufferView < 0 || (size_t)a.bufferView >= bvs.size()) {
        err = "Accessor missing bufferView";
        return false;
    }
    if (a.componentType != 5126) {
        err = "Only FLOAT accessors supported for attributes";
        return false;
    }
    if (typeCount(a.type) != comps) {
        err = "Accessor type mismatch";
        return false;
    }
    const auto& bv = bvs[(size_t)a.bufferView];
    size_t stride = bv.byteStride ? bv.byteStride : comps * sizeof(float);
    size_t start = bv.byteOffset + a.byteOffset;
    if (a.count == 0) {
        out.clear();
        return true;
    }

    size_t need = start + stride * (size_t)(a.count - 1) + comps * sizeof(float);
    if (need > bin.size()) {
        err = "Accessor out of range";
        return false;
    }

    out.resize((size_t)a.count * comps);
    for (size_t i = 0; i < a.count; i++) {
        const uint8_t* p = bin.data() + start + i * stride;
        for (size_t c = 0; c < comps; c++) {
            out[i * comps + c] = readLE<float>(p + c * sizeof(float));
        }
    }
    return true;
}

static bool readAccessorIndices(const std::vector<uint8_t>& bin,
                                const std::vector<BufferView>& bvs,
                                const Accessor& a,
                                std::vector<uint32_t>& out,
                                std::string& err)
{
    if (a.bufferView < 0 || (size_t)a.bufferView >= bvs.size()) {
        err = "Indices missing bufferView";
        return false;
    }
    if (typeCount(a.type) != 1) {
        err = "Indices must be SCALAR";
        return false;
    }
    const auto& bv = bvs[(size_t)a.bufferView];
    size_t comps = 1;
    size_t cs = componentSize(a.componentType);
    if (cs == 0) {
        err = "Unsupported index componentType";
        return false;
    }
    size_t stride = bv.byteStride ? bv.byteStride : cs * comps;
    size_t start = bv.byteOffset + a.byteOffset;
    if (a.count == 0) {
        out.clear();
        return true;
    }
    size_t need = start + stride * (size_t)(a.count - 1) + cs * comps;
    if (need > bin.size()) {
        err = "Indices out of range";
        return false;
    }

    out.resize((size_t)a.count);
    for (size_t i = 0; i < a.count; i++) {
        const uint8_t* p = bin.data() + start + i * stride;
        uint32_t v = 0;
        switch (a.componentType) {
            case 5121:
                v = (uint32_t)readLE<uint8_t>(p);
                break;
            case 5123:
                v = (uint32_t)readLE<uint16_t>(p);
                break;
            case 5125:
                v = (uint32_t)readLE<uint32_t>(p);
                break;
            default:
                err = "Unsupported index type";
                return false;
        }
        out[i] = v;
    }
    return true;
}

static void gatherNodeRecursive(int nodeIndex,
                                const std::vector<mini_json::Value>& nodes,
                                const std::vector<mini_json::Value>& meshes,
                                const std::vector<BufferView>& bvs,
                                const std::vector<Accessor>& accs,
                                const std::vector<uint8_t>& bin,
                                glm::mat4 parent,
                                std::vector<Vertex>& outV,
                                std::vector<uint32_t>& outI,
                                std::string& err,
                                bool& ok)
{
    if (!ok)
        return;
    if (nodeIndex < 0 || (size_t)nodeIndex >= nodes.size())
        return;
    const auto& node = nodes[(size_t)nodeIndex];
    if (!node.is_obj())
        return;

    glm::mat4 M = parent * nodeLocalMatrix(node);

    if (auto* meshIdxV = node.get("mesh"); meshIdxV && meshIdxV->is_num()) {
        int meshIdx = (int)meshIdxV->as_num();
        if (meshIdx >= 0 && (size_t)meshIdx < meshes.size()) {
            const auto& mesh = meshes[(size_t)meshIdx].as_obj();
            auto itPrims = mesh.find("primitives");
            if (itPrims != mesh.end() && itPrims->second.is_arr()) {
                for (auto& primV : itPrims->second.as_arr()) {
                    if (!primV.is_obj())
                        continue;
                    auto& prim = primV.as_obj();

                    if (auto it = prim.find("mode"); it != prim.end() && it->second.is_num()) {
                        if ((int)it->second.as_num() != 4)
                            continue;
                    }

                    auto itAttr = prim.find("attributes");
                    if (itAttr == prim.end() || !itAttr->second.is_obj())
                        continue;
                    auto& attr = itAttr->second.as_obj();

                    auto itPos = attr.find("POSITION");
                    if (itPos == attr.end())
                        continue;

                    int accPos = (int)itPos->second.as_num();
                    int accNrm = -1, accUv = -1;
                    if (auto it = attr.find("NORMAL"); it != attr.end() && it->second.is_num())
                        accNrm = (int)it->second.as_num();
                    if (auto it = attr.find("TEXCOORD_0"); it != attr.end() && it->second.is_num())
                        accUv = (int)it->second.as_num();

                    if (accPos < 0 || (size_t)accPos >= accs.size()) {
                        err = "Bad POSITION accessor";
                        ok = false;
                        return;
                    }

                    std::vector<float> pos, nrm, uv;
                    if (!readAccessorVec(bin, bvs, accs[(size_t)accPos], pos, 3, err)) {
                        ok = false;
                        return;
                    }
                    if (accNrm >= 0) {
                        if ((size_t)accNrm >= accs.size()) {
                            err = "Bad NORMAL accessor";
                            ok = false;
                            return;
                        }
                        if (!readAccessorVec(bin, bvs, accs[(size_t)accNrm], nrm, 3, err)) {
                            ok = false;
                            return;
                        }
                    }
                    if (accUv >= 0) {
                        if ((size_t)accUv >= accs.size()) {
                            err = "Bad TEXCOORD_0 accessor";
                            ok = false;
                            return;
                        }
                        if (!readAccessorVec(bin, bvs, accs[(size_t)accUv], uv, 2, err)) {
                            ok = false;
                            return;
                        }
                    }

                    std::vector<uint32_t> idx;
                    auto itIdx = prim.find("indices");
                    if (itIdx != prim.end() && itIdx->second.is_num()) {
                        int accI = (int)itIdx->second.as_num();
                        if (accI < 0 || (size_t)accI >= accs.size()) {
                            err = "Bad indices accessor";
                            ok = false;
                            return;
                        }
                        if (!readAccessorIndices(bin, bvs, accs[(size_t)accI], idx, err)) {
                            ok = false;
                            return;
                        }
                    } else {
                        idx.resize(pos.size() / 3);
                        for (uint32_t i = 0; i < (uint32_t)idx.size(); i++)
                            idx[i] = i;
                    }

                    uint32_t base = (uint32_t)outV.size();
                    outV.resize(outV.size() + pos.size() / 3);

                    glm::mat3 NrmM = glm::transpose(glm::inverse(glm::mat3(M)));

                    for (size_t i = 0; i < pos.size() / 3; i++) {
                        glm::vec3 P(pos[i * 3 + 0], pos[i * 3 + 1], pos[i * 3 + 2]);
                        glm::vec3 Pw = glm::vec3(M * glm::vec4(P, 1.0f));
                        glm::vec3 Nw(0, 1, 0);
                        if (!nrm.empty()) {
                            glm::vec3 N(nrm[i * 3 + 0], nrm[i * 3 + 1], nrm[i * 3 + 2]);
                            Nw = glm::normalize(NrmM * N);
                        }
                        glm::vec2 Uv(0.0f);
                        if (!uv.empty())
                            Uv = { uv[i * 2 + 0], uv[i * 2 + 1] };
                        outV[base + (uint32_t)i] = Vertex{ Pw, Nw, Uv };
                    }

                    outI.reserve(outI.size() + idx.size());

                    uint32_t localVertCount = (uint32_t)(pos.size() / 3);
                    for (auto v : idx) {
                        if (v >= localVertCount) {
                            err = "Index out of range for primitive";
                            ok = false;
                            return;
                        }
                        outI.push_back(base + v);
                    }
                }
            }
        }
    }

    if (auto* ch = node.get("children"); ch && ch->is_arr()) {
        for (auto& c : ch->as_arr()) {
            if (!c.is_num())
                continue;
            gatherNodeRecursive((int)c.as_num(), nodes, meshes, bvs, accs, bin, M, outV, outI, err, ok);
        }
    }
}

}  // namespace

bool loadGltfScene(const std::string& path, GltfSceneData& out, std::string& err)
{
    out = {};
    std::string jsonText;
    if (!readFileText(path, jsonText, err))
        return false;

    mini_json::Value root;
    try {
        root = mini_json::parse(jsonText);
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
    if (!root.is_obj()) {
        err = "Root JSON not object";
        return false;
    }

    auto* buffers = root.get("buffers");
    if (!buffers || !buffers->is_arr() || buffers->as_arr().empty()) {
        err = "No buffers";
        return false;
    }
    auto& b0 = buffers->as_arr()[0].as_obj();
    auto itUri = b0.find("uri");
    if (itUri == b0.end() || !itUri->second.is_str()) {
        err = "buffers[0].uri missing (export as .gltf + .bin)";
        return false;
    }
    std::string binPath = dirOf(path) + itUri->second.as_str();

    std::vector<uint8_t> bin;
    if (!readFileBin(binPath, bin, err))
        return false;

    std::vector<BufferView> bvs;
    std::vector<Accessor> accs;
    std::vector<mini_json::Value> const *meshes = nullptr, *nodes = nullptr, *scenes = nullptr;
    if (!loadArrays(root, bvs, accs, meshes, nodes, scenes, err))
        return false;

    std::vector<std::string> imageUris;
    if (auto* imgs = root.get("images"); imgs && imgs->is_arr()) {
        imageUris.reserve(imgs->as_arr().size());
        for (auto& iv : imgs->as_arr()) {
            if (!iv.is_obj()) {
                imageUris.emplace_back();
                continue;
            }
            auto& io = iv.as_obj();
            auto it = io.find("uri");
            if (it != io.end() && it->second.is_str())
                imageUris.push_back(it->second.as_str());
            else
                imageUris.emplace_back();
        }
    }
    std::vector<int> texToImage;
    if (auto* tex = root.get("textures"); tex && tex->is_arr()) {
        texToImage.resize(tex->as_arr().size(), -1);
        for (size_t i = 0; i < tex->as_arr().size(); ++i) {
            auto& tv = tex->as_arr()[i];
            if (!tv.is_obj())
                continue;
            auto& to = tv.as_obj();
            auto it = to.find("source");
            if (it != to.end() && it->second.is_num())
                texToImage[i] = (int)it->second.as_num();
        }
    }
    auto resolveTextureUri = [&](int texIndex) -> std::string {
        if (texIndex < 0 || (size_t)texIndex >= texToImage.size())
            return {};
        int imgIndex = texToImage[(size_t)texIndex];
        if (imgIndex < 0 || (size_t)imgIndex >= imageUris.size())
            return {};
        return imageUris[(size_t)imgIndex];
    };

    int materialIndex = 0;
    if (meshes && !meshes->empty()) {
        const auto& mo = (*meshes)[0].as_obj();
        auto itP = mo.find("primitives");
        if (itP != mo.end() && itP->second.is_arr() && !itP->second.as_arr().empty()) {
            const auto& prim = itP->second.as_arr()[0].as_obj();
            auto itM = prim.find("material");
            if (itM != prim.end() && itM->second.is_num())
                materialIndex = (int)itM->second.as_num();
        }
    }

    if (auto* mats = root.get("materials"); mats && mats->is_arr() && !mats->as_arr().empty()) {
        if (materialIndex < 0 || (size_t)materialIndex >= mats->as_arr().size())
            materialIndex = 0;
        const auto& mat = mats->as_arr()[(size_t)materialIndex].as_obj();

        if (auto itP = mat.find("pbrMetallicRoughness"); itP != mat.end() && itP->second.is_obj()) {
            const auto& pbr = itP->second.as_obj();

            if (auto it = pbr.find("baseColorFactor"); it != pbr.end() && it->second.is_arr()) {
                auto& a = it->second.as_arr();
                if (a.size() >= 4 && a[0].is_num() && a[1].is_num() && a[2].is_num() && a[3].is_num())
                    out.material.baseColorFactor =
                        glm::vec4((float)a[0].as_num(), (float)a[1].as_num(), (float)a[2].as_num(), (float)a[3].as_num());
            }
            if (auto it = pbr.find("metallicFactor"); it != pbr.end() && it->second.is_num())
                out.material.metallicFactor = (float)it->second.as_num();
            if (auto it = pbr.find("roughnessFactor"); it != pbr.end() && it->second.is_num())
                out.material.roughnessFactor = (float)it->second.as_num();

            if (auto it = pbr.find("baseColorTexture"); it != pbr.end() && it->second.is_obj()) {
                const auto& t = it->second.as_obj();
                auto itI = t.find("index");
                if (itI != t.end() && itI->second.is_num())
                    out.material.baseColorUri = resolveTextureUri((int)itI->second.as_num());
            }
            if (auto it = pbr.find("metallicRoughnessTexture"); it != pbr.end() && it->second.is_obj()) {
                const auto& t = it->second.as_obj();
                auto itI = t.find("index");
                if (itI != t.end() && itI->second.is_num())
                    out.material.metallicRoughnessUri = resolveTextureUri((int)itI->second.as_num());
            }
        }

        if (auto it = mat.find("normalTexture"); it != mat.end() && it->second.is_obj()) {
            const auto& t = it->second.as_obj();
            auto itI = t.find("index");
            if (itI != t.end() && itI->second.is_num())
                out.material.normalUri = resolveTextureUri((int)itI->second.as_num());
        }
    }

    int sceneIndex = 0;
    if (auto* s = root.get("scene"); s && s->is_num())
        sceneIndex = (int)s->as_num();
    if (sceneIndex < 0 || (size_t)sceneIndex >= scenes->size())
        sceneIndex = 0;

    const auto& scene = (*scenes)[(size_t)sceneIndex].as_obj();
    auto itNodes = scene.find("nodes");
    if (itNodes == scene.end() || !itNodes->second.is_arr()) {
        err = "Scene has no nodes";
        return false;
    }

    bool ok = true;
    glm::mat4 I(1.0f);
    for (auto& n : itNodes->second.as_arr()) {
        if (!n.is_num())
            continue;
        gatherNodeRecursive((int)n.as_num(), *nodes, *meshes, bvs, accs, bin, I, out.vertices, out.indices, err, ok);
    }

    if (!ok)
        return false;
    if (out.vertices.empty() || out.indices.empty()) {
        err = "Loaded glTF but got no triangles";
        return false;
    }

    {
        std::vector<glm::vec3> tan1(out.vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> tan2(out.vertices.size(), glm::vec3(0.0f));

        for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
            uint32_t i0 = out.indices[i + 0];
            uint32_t i1 = out.indices[i + 1];
            uint32_t i2 = out.indices[i + 2];
            if (i0 >= out.vertices.size() || i1 >= out.vertices.size() || i2 >= out.vertices.size())
                continue;

            const glm::vec3& p0 = out.vertices[i0].pos;
            const glm::vec3& p1 = out.vertices[i1].pos;
            const glm::vec3& p2 = out.vertices[i2].pos;
            const glm::vec2& w0 = out.vertices[i0].uv;
            const glm::vec2& w1 = out.vertices[i1].uv;
            const glm::vec2& w2 = out.vertices[i2].uv;

            glm::vec3 e1 = p1 - p0;
            glm::vec3 e2 = p2 - p0;
            glm::vec2 d1 = w1 - w0;
            glm::vec2 d2 = w2 - w0;

            float r = (d1.x * d2.y - d1.y * d2.x);
            if (fabs(r) < 1e-20f)
                continue;
            r = 1.0f / r;

            glm::vec3 sdir = (e1 * d2.y - e2 * d1.y) * r;
            glm::vec3 tdir = (e2 * d1.x - e1 * d2.x) * r;

            tan1[i0] += sdir;
            tan1[i1] += sdir;
            tan1[i2] += sdir;

            tan2[i0] += tdir;
            tan2[i1] += tdir;
            tan2[i2] += tdir;
        }

        for (size_t i = 0; i < out.vertices.size(); ++i) {
            glm::vec3 n = out.vertices[i].nrm;
            glm::vec3 t = tan1[i];

            t = glm::normalize(t - n * glm::dot(n, t));
            if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z) || glm::length(t) < 1e-6f)
                t = glm::vec3(1.0f, 0.0f, 0.0f);

            float w = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
            out.vertices[i].tangent = glm::vec4(t, w);
        }
    }

    return true;
}
