#include "ObjLoader.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace {

struct Key {
    int v = 0;
    int vt = 0;
    int vn = 0;
    bool operator==(const Key& o) const { return v == o.v && vt == o.vt && vn == o.vn; }
};

struct KeyHash {
    size_t operator()(const Key& k) const noexcept { return (size_t)k.v * 73856093u ^ (size_t)k.vt * 19349663u ^ (size_t)k.vn * 83492791u; }
};

static bool parseInt(const std::string_view& s, int& out)
{
    const char* b = s.data();
    const char* e = s.data() + s.size();
    auto r = std::from_chars(b, e, out);
    return r.ec == std::errc{};
}

static int fixIndex(int idx, int count)
{
    if (idx > 0)
        return idx - 1;
    if (idx < 0)
        return count + idx;
    return -1;
}

static bool parseVertexTriplet(const std::string_view& token, int& v, int& vt, int& vn)
{
    v = vt = vn = 0;

    size_t s1 = token.find('/');
    if (s1 == std::string_view::npos) {
        return parseInt(token, v);
    }
    size_t s2 = token.find('/', s1 + 1);
    std::string_view a = token.substr(0, s1);
    std::string_view b = (s2 == std::string_view::npos) ? token.substr(s1 + 1) : token.substr(s1 + 1, s2 - (s1 + 1));
    std::string_view c = (s2 == std::string_view::npos) ? std::string_view{} : token.substr(s2 + 1);

    if (!a.empty() && !parseInt(a, v))
        return false;
    if (!b.empty() && !parseInt(b, vt))
        return false;
    if (!c.empty() && !parseInt(c, vn))
        return false;
    return true;
}

}  // namespace

bool loadObj(const std::string& path, ObjMeshData& out, std::string& error)
{
    out = {};
    error.clear();

    std::ifstream f(path);
    if (!f) {
        error = "loadObj: can't open " + path;
        return false;
    }

    std::vector<glm::vec3> pos;
    std::vector<glm::vec3> nrm;
    std::vector<glm::vec2> uv;

    std::unordered_map<Key, uint32_t, KeyHash> dedup;

    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream iss(line);
        std::string op;
        iss >> op;
        if (op == "v") {
            glm::vec3 p{};
            iss >> p.x >> p.y >> p.z;
            pos.push_back(p);
        } else if (op == "vn") {
            glm::vec3 n{};
            iss >> n.x >> n.y >> n.z;
            nrm.push_back(n);
        } else if (op == "vt") {
            glm::vec2 t{};
            iss >> t.x >> t.y;

            t.y = 1.0f - t.y;
            uv.push_back(t);
        } else if (op == "f") {
            std::vector<std::string> toks;
            std::string tok;
            while (iss >> tok)
                toks.push_back(tok);
            if (toks.size() < 3)
                continue;

            auto emit = [&](const std::string& t) -> uint32_t {
                int iv = 0, ivt = 0, ivn = 0;
                if (!parseVertexTriplet(t, iv, ivt, ivn)) {
                    throw std::runtime_error("OBJ parse error at line " + std::to_string(lineNo));
                }
                Key k{ iv, ivt, ivn };
                auto it = dedup.find(k);
                if (it != dedup.end())
                    return it->second;

                ObjVertex vtx{};
                int pv = fixIndex(iv, (int)pos.size());
                int pt = fixIndex(ivt, (int)uv.size());
                int pn = fixIndex(ivn, (int)nrm.size());
                if (pv >= 0)
                    vtx.pos = pos[(size_t)pv];
                if (pt >= 0)
                    vtx.uv = uv[(size_t)pt];
                if (pn >= 0)
                    vtx.nrm = nrm[(size_t)pn];

                uint32_t idx = (uint32_t)out.vertices.size();
                out.vertices.push_back(vtx);
                dedup.insert({ k, idx });
                return idx;
            };

            uint32_t i0 = emit(toks[0]);
            for (size_t i = 1; i + 1 < toks.size(); ++i) {
                uint32_t i1 = emit(toks[i]);
                uint32_t i2 = emit(toks[i + 1]);
                out.indices.push_back(i0);
                out.indices.push_back(i1);
                out.indices.push_back(i2);
            }
        }
    }

    if (out.vertices.empty() || out.indices.empty()) {
        error = "loadObj: no geometry in " + path;
        return false;
    }

    return true;
}
