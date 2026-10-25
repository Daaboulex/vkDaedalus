#include "shader_cache.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include "reshade/effect_preprocessor.hpp"
#include "reshade/effect_parser.hpp"
#include "reshade/effect_codegen.hpp"

#include "config_paths.hpp"
#include "layer_lock.hpp"
#include "logger.hpp"
#include "reshade_fx_env.hpp"

namespace vkBasalt
{
    std::string cacheEntryTempPath(const std::string& entryPath)
    {
        static std::atomic<uint64_t> writes{0};
        return entryPath + "." + std::to_string(getpid()) + "." + std::to_string(writes++) + ".tmp";
    }

    namespace
    {
        constexpr uint32_t MAGIC = 0x43424B56;
        constexpr size_t MEMORY_CACHE_CAP = 16;
        constexpr size_t DISK_CACHE_CAP = 256;
        constexpr uint32_t MAX_COUNT = 1u << 24;

        uint64_t fnv1a(const std::string& data, uint64_t hash)
        {
            for (unsigned char c : data)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string hashName(const std::string& blob)
        {
            uint64_t a = fnv1a(blob, 14695981039346656037ull);
            uint64_t b = fnv1a(blob, 1099511628211ull * 31 + 7);
            char buf[33];
            snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)a, (unsigned long long)b);
            return buf;
        }

        uint64_t contentHash(const std::string& data)
        {
            return fnv1a(data, 14695981039346656037ull);
        }

        bool readFileBytes(const std::string& path, std::string& out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f.is_open())
                return false;
            std::ostringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return !f.bad();
        }

        const std::string& buildIdentity()
        {
            static const std::string identity = [] {
                Dl_info     info{};
                std::string path = "/proc/self/exe";
                if (dladdr(reinterpret_cast<const void*>(&buildIdentity), &info) != 0 && info.dli_fname && info.dli_fname[0] == '/')
                    path = info.dli_fname;
                std::string bytes;
                if (!readFileBytes(path, bytes) || bytes.empty())
                {
                    Logger::warn("compiled effects: " + path + " cannot be read to tell this build from others, so compiled effects stay in memory only");
                    return std::string();
                }
                return hashName(bytes);
            }();
            return identity;
        }

        struct Writer
        {
            std::string buf;

            void u8(uint8_t v) { buf.push_back((char)v); }
            void u32(uint32_t v) { buf.append((const char*)&v, 4); }
            void u64(uint64_t v) { buf.append((const char*)&v, 8); }
            void i32(int32_t v) { buf.append((const char*)&v, 4); }
            void f32(float v) { buf.append((const char*)&v, 4); }
            void str(const std::string& s)
            {
                u32((uint32_t)s.size());
                buf.append(s);
            }
            void bytes(const void* p, size_t n) { buf.append((const char*)p, n); }
        };

        struct Reader
        {
            const std::string& buf;
            size_t pos = 0;

            explicit Reader(const std::string& b) : buf(b) {}

            void need(size_t n)
            {
                if (pos + n > buf.size())
                    throw std::runtime_error("cache entry truncated");
            }
            uint8_t u8()
            {
                need(1);
                return (uint8_t)buf[pos++];
            }
            uint32_t u32()
            {
                need(4);
                uint32_t v;
                std::memcpy(&v, buf.data() + pos, 4);
                pos += 4;
                return v;
            }
            uint64_t u64()
            {
                need(8);
                uint64_t v;
                std::memcpy(&v, buf.data() + pos, 8);
                pos += 8;
                return v;
            }
            int32_t i32()
            {
                need(4);
                int32_t v;
                std::memcpy(&v, buf.data() + pos, 4);
                pos += 4;
                return v;
            }
            float f32()
            {
                need(4);
                float v;
                std::memcpy(&v, buf.data() + pos, 4);
                pos += 4;
                return v;
            }
            uint32_t count()
            {
                uint32_t n = u32();
                if (n > MAX_COUNT)
                    throw std::runtime_error("cache entry count out of range");
                return n;
            }
            std::string str()
            {
                uint32_t n = count();
                need(n);
                std::string s = buf.substr(pos, n);
                pos += n;
                return s;
            }
            void bytes(void* p, size_t n)
            {
                need(n);
                std::memcpy(p, buf.data() + pos, n);
                pos += n;
            }
        };

        void putType(Writer& w, const reshadefx::type& t)
        {
            w.u8((uint8_t)t.base);
            w.u32(t.rows);
            w.u32(t.cols);
            w.u32(t.qualifiers);
            w.i32(t.array_length);
            w.u32(t.struct_definition);
        }

        reshadefx::type getType(Reader& r)
        {
            reshadefx::type t;
            t.base = (reshadefx::type::datatype)r.u8();
            t.rows = r.u32();
            t.cols = r.u32();
            t.qualifiers = r.u32();
            t.array_length = r.i32();
            t.struct_definition = r.u32();
            return t;
        }

        void putConstant(Writer& w, const reshadefx::constant& c)
        {
            w.bytes(c.as_uint, sizeof(c.as_uint));
            w.str(c.string_data);
            w.u32((uint32_t)c.array_data.size());
            for (const auto& e : c.array_data)
                putConstant(w, e);
        }

        reshadefx::constant getConstant(Reader& r)
        {
            reshadefx::constant c;
            r.bytes(c.as_uint, sizeof(c.as_uint));
            c.string_data = r.str();
            uint32_t n = r.count();
            c.array_data.resize(n);
            for (uint32_t i = 0; i < n; i++)
                c.array_data[i] = getConstant(r);
            return c;
        }

        void putAnnotations(Writer& w, const std::vector<reshadefx::annotation>& v)
        {
            w.u32((uint32_t)v.size());
            for (const auto& a : v)
            {
                putType(w, a.type);
                w.str(a.name);
                putConstant(w, a.value);
            }
        }

        std::vector<reshadefx::annotation> getAnnotations(Reader& r)
        {
            uint32_t n = r.count();
            std::vector<reshadefx::annotation> v(n);
            for (uint32_t i = 0; i < n; i++)
            {
                v[i].type = getType(r);
                v[i].name = r.str();
                v[i].value = getConstant(r);
            }
            return v;
        }

        void putModule(Writer& w, const reshadefx::effect_module& m)
        {

            w.u32((uint32_t)m.entry_points.size());
            for (const auto& e : m.entry_points)
            {
                w.str(e.first);
                w.u8((uint8_t) e.second);
            }

            w.u32((uint32_t)m.textures.size());
            for (const auto& t : m.textures)
            {
                w.u32(t.id);
                w.str(t.name);
                w.str(t.semantic);
                w.str(t.unique_name);
                putAnnotations(w, t.annotations);
                w.u32(t.width);
                w.u32(t.height);
                w.u32(t.depth);
                w.u32(t.levels);
                w.u8((uint8_t)t.type);
                w.u32((uint32_t)t.format);
                w.u8(t.render_target ? 1 : 0);
                w.u8(t.storage_access ? 1 : 0);
                w.u32(t.semantic_binding);
            }

            w.u32((uint32_t)m.samplers.size());
            for (const auto& s : m.samplers)
            {
                putType(w, s.type);
                w.u32(s.id);
                w.str(s.name);
                w.str(s.unique_name);
                w.str(s.texture_name);
                putAnnotations(w, s.annotations);
                w.u32((uint32_t)s.filter);
                w.u32((uint32_t)s.address_u);
                w.u32((uint32_t)s.address_v);
                w.u32((uint32_t)s.address_w);
                w.f32(s.min_lod);
                w.f32(s.max_lod);
                w.f32(s.lod_bias);
                w.u8(s.srgb);
            }

            w.u32((uint32_t)m.storages.size());
            for (const auto& s : m.storages)
            {
                putType(w, s.type);
                w.u32(s.id);
                w.str(s.name);
                w.str(s.unique_name);
                w.str(s.texture_name);
                w.u32(s.level);
            }

            auto putUniforms = [&](const std::vector<reshadefx::uniform>& v) {
                w.u32((uint32_t)v.size());
                for (const auto& u : v)
                {
                    w.str(u.name);
                    putType(w, u.type);
                    w.u32(u.size);
                    w.u32(u.offset);
                    putAnnotations(w, u.annotations);
                    w.u8(u.has_initializer_value ? 1 : 0);
                    putConstant(w, u.initializer_value);
                }
            };
            putUniforms(m.uniforms);
            putUniforms(m.spec_constants);

            w.u32((uint32_t)m.techniques.size());
            for (const auto& t : m.techniques)
            {
                w.str(t.name);
                putAnnotations(w, t.annotations);
                w.u32((uint32_t)t.passes.size());
                for (const auto& p : t.passes)
                {
                    for (int i = 0; i < 8; i++)
                        w.str(p.render_target_names[i]);
                    w.str(p.vs_entry_point);
                    w.str(p.ps_entry_point);
                    w.str(p.cs_entry_point);
                    w.u8(p.generate_mipmaps);
                    w.u8(p.clear_render_targets);
                    w.u8(p.srgb_write_enable);
                    w.u8(p.blend_enable[0]);
                    w.u8(p.stencil_enable);
                    w.u8(p.render_target_write_mask[0]);
                    w.u8(p.stencil_read_mask);
                    w.u8(p.stencil_write_mask);
                    w.u8((uint8_t)p.color_blend_op[0]);
                    w.u8((uint8_t)p.alpha_blend_op[0]);
                    w.u8((uint8_t)p.source_color_blend_factor[0]);
                    w.u8((uint8_t)p.dest_color_blend_factor[0]);
                    w.u8((uint8_t)p.source_alpha_blend_factor[0]);
                    w.u8((uint8_t)p.dest_alpha_blend_factor[0]);
                    w.u8((uint8_t)p.stencil_comparison_func);
                    w.u32(p.stencil_reference_value);
                    w.u8((uint8_t)p.stencil_pass_op);
                    w.u8((uint8_t)p.stencil_fail_op);
                    w.u8((uint8_t)p.stencil_depth_fail_op);
                    w.u32(p.num_vertices);
                    w.u8((uint8_t)p.topology);
                    w.u32(p.viewport_width);
                    w.u32(p.viewport_height);
                    w.u32(p.viewport_dispatch_z);
                }
            }

            w.u32(m.total_uniform_size);
        }

        reshadefx::effect_module getModule(Reader& r)
        {
            reshadefx::effect_module m;

            uint32_t n = r.count();
            m.entry_points.resize(n);
            for (auto& e : m.entry_points)
            {
                e.first = r.str();
                e.second = (reshadefx::shader_type) r.u8();
            }

            n = r.count();
            m.textures.resize(n);
            for (auto& t : m.textures)
            {
                t.id = r.u32();
                t.name = r.str();
                t.semantic = r.str();
                t.unique_name = r.str();
                t.annotations = getAnnotations(r);
                t.width = r.u32();
                t.height = r.u32();
                t.depth = (uint16_t)r.u32();
                t.levels = (uint16_t)r.u32();
                t.type = (reshadefx::texture_type)r.u8();
                t.format = (reshadefx::texture_format)r.u32();
                t.render_target = r.u8() != 0;
                t.storage_access = r.u8() != 0;
                t.semantic_binding = r.u32();
            }

            n = r.count();
            m.samplers.resize(n);
            for (auto& s : m.samplers)
            {
                s.type = getType(r);
                s.id = r.u32();
                s.name = r.str();
                s.unique_name = r.str();
                s.texture_name = r.str();
                s.annotations = getAnnotations(r);
                s.filter = (reshadefx::filter_mode)r.u32();
                s.address_u = (reshadefx::texture_address_mode)r.u32();
                s.address_v = (reshadefx::texture_address_mode)r.u32();
                s.address_w = (reshadefx::texture_address_mode)r.u32();
                s.min_lod = r.f32();
                s.max_lod = r.f32();
                s.lod_bias = r.f32();
                s.srgb = r.u8();
            }

            n = r.count();
            m.storages.resize(n);
            for (auto& s : m.storages)
            {
                s.type = getType(r);
                s.id = r.u32();
                s.name = r.str();
                s.unique_name = r.str();
                s.texture_name = r.str();
                s.level = (uint16_t)r.u32();
            }

            auto getUniforms = [&](std::vector<reshadefx::uniform>& v) {
                uint32_t k = r.count();
                v.resize(k);
                for (auto& u : v)
                {
                    u.name = r.str();
                    u.type = getType(r);
                    u.size = r.u32();
                    u.offset = r.u32();
                    u.annotations = getAnnotations(r);
                    u.has_initializer_value = r.u8() != 0;
                    u.initializer_value = getConstant(r);
                }
            };
            getUniforms(m.uniforms);
            getUniforms(m.spec_constants);

            n = r.count();
            m.techniques.resize(n);
            for (auto& t : m.techniques)
            {
                t.name = r.str();
                t.annotations = getAnnotations(r);
                uint32_t pc = r.count();
                t.passes.resize(pc);
                for (auto& p : t.passes)
                {
                    for (int i = 0; i < 8; i++)
                        p.render_target_names[i] = r.str();
                    p.vs_entry_point = r.str();
                    p.ps_entry_point = r.str();
                    p.cs_entry_point = r.str();
                    p.generate_mipmaps = r.u8() != 0;
                    p.clear_render_targets = r.u8();
                    p.srgb_write_enable = r.u8();
                    p.blend_enable[0] = r.u8();
                    p.stencil_enable = r.u8();
                    p.render_target_write_mask[0] = r.u8();
                    p.stencil_read_mask = r.u8();
                    p.stencil_write_mask = r.u8();
                    p.color_blend_op[0] = (reshadefx::blend_op)r.u8();
                    p.alpha_blend_op[0] = (reshadefx::blend_op)r.u8();
                    p.source_color_blend_factor[0] = (reshadefx::blend_factor)r.u8();
                    p.dest_color_blend_factor[0] = (reshadefx::blend_factor)r.u8();
                    p.source_alpha_blend_factor[0] = (reshadefx::blend_factor)r.u8();
                    p.dest_alpha_blend_factor[0] = (reshadefx::blend_factor)r.u8();
                    p.stencil_comparison_func = (reshadefx::stencil_func)r.u8();
                    p.stencil_reference_value = r.u32();
                    p.stencil_pass_op = (reshadefx::stencil_op)r.u8();
                    p.stencil_fail_op = (reshadefx::stencil_op)r.u8();
                    p.stencil_depth_fail_op = (reshadefx::stencil_op)r.u8();
                    p.num_vertices = r.u32();
                    p.topology = (reshadefx::primitive_topology)r.u8();
                    p.viewport_width = r.u32();
                    p.viewport_height = r.u32();
                    p.viewport_dispatch_z = r.u32();
                }
            }

            m.total_uniform_size = r.u32();
            return m;
        }

        std::string entryInconsistency(const CompiledReshadeEffect& e)
        {
            const reshadefx::effect_module& m = e.module;
            if (m.techniques.empty())
                return "the effect has no technique";
            if (e.entryPointSpirv.empty())
                return "the effect has no SPIR-V";
            for (const reshadefx::uniform& u : m.uniforms)
            {
                if (u.offset > m.total_uniform_size || u.size > m.total_uniform_size - u.offset)
                    return "uniform " + u.name + " lies outside the uniform block";
            }

            const auto isTexture = [&](const std::string& name) {
                return std::any_of(m.textures.begin(), m.textures.end(), [&](const reshadefx::texture& t) { return t.unique_name == name; });
            };
            for (const reshadefx::sampler& s : m.samplers)
            {
                if (!isTexture(s.texture_name))
                    return "sampler " + s.unique_name + " samples " + s.texture_name + ", which is not a texture";
            }
            for (const reshadefx::storage& s : m.storages)
            {
                if (!isTexture(s.texture_name))
                    return "storage " + s.unique_name + " writes " + s.texture_name + ", which is not a texture";
            }

            for (const reshadefx::technique& t : m.techniques)
            {
                for (size_t p = 0; p < t.passes.size(); p++)
                {
                    const reshadefx::pass& pass  = t.passes[p];
                    const std::string      where = "pass " + std::to_string(p) + " of technique " + t.name;
                    if (pass.cs_entry_point.empty() && (pass.vs_entry_point.empty() || pass.ps_entry_point.empty()))
                        return where + " has neither a compute shader nor a vertex and a pixel shader";
                    for (const std::string* entryPoint : {&pass.vs_entry_point, &pass.ps_entry_point, &pass.cs_entry_point})
                    {
                        if (!entryPoint->empty() && !e.entryPointSpirv.count(*entryPoint))
                            return where + " names entry point " + *entryPoint + ", which has no SPIR-V";
                    }
                    for (const std::string& target : pass.render_target_names)
                    {
                        if (!target.empty() && !isTexture(target))
                            return where + " renders to " + target + ", which is not a texture";
                    }
                }
            }
            return {};
        }

        std::string serializeBody(const CompiledReshadeEffect& e)
        {
            Writer w;
            w.u32((uint32_t)e.includedFiles.size());
            for (const auto& [path, hash] : e.includedFiles)
            {
                w.str(path);
                w.u64(hash);
            }
            w.u32((uint32_t)e.includeResolutions.size());
            for (const IncludeResolution& resolution : e.includeResolutions)
            {
                w.str(resolution.source);
                w.str(resolution.name);
                w.str(resolution.path);
                w.u8(resolution.found ? 1 : 0);
            }

            w.str(e.error);
            if (!e.error.empty())
                return w.buf;

            w.u32((uint32_t)e.usedMacros.size());
            for (const auto& [name, value] : e.usedMacros)
            {
                w.str(name);
                w.str(value);
            }
            w.u8(e.usesDepth ? 1 : 0);
            w.u8(e.usesMinPrecision ? 1 : 0);
            w.str(e.warnings);
            putModule(w, e.module);
            w.u32((uint32_t) e.entryPointSpirv.size());
            for (const auto& [entryPointName, words] : e.entryPointSpirv)
            {
                w.str(entryPointName);
                w.u32((uint32_t) words.size());
                w.bytes(words.data(), words.size() * sizeof(uint32_t));
            }
            return w.buf;
        }

        std::string serializeEntry(const CompiledReshadeEffect& e)
        {
            const std::string body = serializeBody(e);
            Writer            w;
            w.u32(MAGIC);
            w.u64(contentHash(body));
            return w.buf + body;
        }

        std::shared_ptr<CompiledReshadeEffect> deserializeEntry(const std::string& buf)
        {
            Reader r(buf);
            if (r.u32() != MAGIC)
                throw std::runtime_error("not a vkDaedalus shader cache entry");
            const uint64_t checksum = r.u64();
            if (contentHash(buf.substr(r.pos)) != checksum)
                throw std::runtime_error("cache entry is damaged");

            auto e = std::make_shared<CompiledReshadeEffect>();

            uint32_t n = r.count();
            e->includedFiles.resize(n);
            for (auto& [path, hash] : e->includedFiles)
            {
                path = r.str();
                hash = r.u64();
            }
            e->includeResolutions.resize(r.count());
            for (IncludeResolution& resolution : e->includeResolutions)
            {
                resolution.source = r.str();
                resolution.name   = r.str();
                resolution.path   = r.str();
                resolution.found  = r.u8() != 0;
            }

            e->error = r.str();
            if (!e->error.empty())
                return e;

            n = r.count();
            e->usedMacros.resize(n);
            for (auto& [name, value] : e->usedMacros)
            {
                name = r.str();
                value = r.str();
            }
            e->usesDepth = r.u8() != 0;
            e->usesMinPrecision = r.u8() != 0;
            e->warnings = r.str();
            e->module = getModule(r);
            {
                const uint32_t moduleCount = r.count();
                for (uint32_t i = 0; i < moduleCount; i++)
                {
                    std::string entryPointName = r.str();
                    const uint32_t words = r.count();
                    std::vector<uint32_t> code(words);
                    r.bytes(code.data(), words * sizeof(uint32_t));
                    e->entryPointSpirv[std::move(entryPointName)] = std::move(code);
                }
            }
            if (const std::string problem = entryInconsistency(*e); !problem.empty())
                throw std::runtime_error("cache entry: " + problem);
            return e;
        }

        std::string cacheDir()
        {
            const char* xdg = std::getenv("XDG_CACHE_HOME");
            if (xdg && *xdg)
                return std::string(xdg) + "/vkdaedalus";
            const char* home = std::getenv("HOME");
            if (home && *home)
                return std::string(home) + "/.cache/vkdaedalus";
            return "";
        }

        std::string diskCacheDir()
        {
            return buildIdentity().empty() ? std::string() : cacheDir();
        }

        bool ensureCacheDir(const std::string& dir)
        {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            return !ec && std::filesystem::is_directory(dir, ec);
        }

        void pruneDiskCache(const std::string& dir)
        {
            std::error_code ec;
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> entries;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (entry.path().extension() != ".vkbfx")
                    continue;
                std::error_code tec;
                auto t = entry.last_write_time(tec);
                if (!tec)
                    entries.push_back({t, entry.path()});
            }
            if (entries.size() <= DISK_CACHE_CAP)
                return;

            std::sort(entries.begin(), entries.end());
            size_t excess = entries.size() - DISK_CACHE_CAP * 7 / 8;
            for (size_t i = 0; i < excess; i++)
            {
                std::error_code uec;
                std::filesystem::remove(entries[i].second, uec);
            }
            Logger::debug("compiled effects: pruned " + std::to_string(excess) + " old entries");
        }

        bool writeEntryFile(const std::string& dir, const std::string& key, const std::string& payload)
        {
            std::string finalPath = dir + "/" + key + ".vkbfx";
            std::string tmpPath = cacheEntryTempPath(finalPath);

            std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
            if (!f.is_open())
                return false;
            f.write(payload.data(), (std::streamsize)payload.size());
            f.close();
            if (f.fail())
            {
                std::remove(tmpPath.c_str());
                return false;
            }
            if (std::rename(tmpPath.c_str(), finalPath.c_str()) != 0)
            {
                std::remove(tmpPath.c_str());
                return false;
            }
            return true;
        }

        bool includesUnchanged(const CompiledReshadeEffect& e, const std::vector<std::string>& includePaths)
        {
            for (const auto& [path, hash] : e.includedFiles)
            {
                std::string content;
                if (!readFileBytes(path, content))
                    return false;
                if (contentHash(content) != hash)
                    return false;
            }

            const std::vector<std::filesystem::path> searchPaths(includePaths.begin(), includePaths.end());
            for (const IncludeResolution& resolution : e.includeResolutions)
            {
                const std::filesystem::path now =
                    reshadefx::preprocessor::resolve_include(std::filesystem::path(resolution.source), resolution.name, searchPaths);
                std::error_code ec;
                if (now.string() != resolution.path || std::filesystem::exists(now, ec) != resolution.found)
                    return false;
            }
            return true;
        }


    }

    constexpr uint32_t spvOpEntryPoint   = 15;
    constexpr uint32_t spvOpFunction     = 54;
    constexpr uint32_t spvOpFunctionEnd  = 56;
    constexpr uint32_t spvOpFunctionCall = 57;
    constexpr uint32_t spvOpLoad         = 61;
    constexpr uint32_t spvOpAccessChain  = 65;
    constexpr uint32_t spvOpInBoundsAccessChain = 66;
    constexpr uint32_t spvOpPtrAccessChain = 67;
    constexpr uint32_t spvOpCopyObject   = 83;

    static bool moduleUsesDepth(const reshadefx::effect_module& module, const std::map<std::string, std::vector<uint32_t>>& entryPointSpirv)
    {
            std::set<std::string> depthTexNames;
            for (const auto& tex : module.textures)
            {
                if (tex.semantic == "DEPTH")
                {
                    depthTexNames.insert(tex.unique_name);
                }
            }
            if (depthTexNames.empty())
                return false;

            auto isSamplerUsedInOneModule = [&](uint32_t samplerId, const std::vector<uint32_t>& code) -> bool {
                if (code.size() < 5)
                    return false;

                std::set<uint32_t> entryFuncIds;
                size_t i = 5;
                while (i < code.size())
                {
                    uint32_t wc = code[i] >> 16;
                    uint32_t op = code[i] & 0xFFFF;
                    if (wc == 0)
                        break;
                    if (op == spvOpEntryPoint && wc >= 3 && (i + 2) < code.size())
                        entryFuncIds.insert(code[i + 2]);
                    i += wc;
                }

                struct FuncInfo
                {
                    bool usesDepthSampler = false;
                    std::vector<uint32_t> callees;
                };
                std::unordered_map<uint32_t, FuncInfo> funcs;
                uint32_t currentFunc = 0;
                i = 5;
                while (i < code.size())
                {
                    uint32_t wc = code[i] >> 16;
                    uint32_t op = code[i] & 0xFFFF;
                    if (wc == 0)
                        break;
                    if (op == spvOpFunction && wc >= 3 && (i + 2) < code.size())
                    {
                        currentFunc = code[i + 2];
                        funcs[currentFunc];
                    }
                    else if (op == spvOpFunctionEnd)
                        currentFunc = 0;
                    else if (currentFunc != 0)
                    {
                        const bool readsThroughPointer = op == spvOpLoad || op == spvOpCopyObject || op == spvOpAccessChain
                                                         || op == spvOpInBoundsAccessChain || op == spvOpPtrAccessChain;
                        if (readsThroughPointer && wc >= 4 && (i + 3) < code.size() && code[i + 3] == samplerId)
                            funcs[currentFunc].usesDepthSampler = true;
                        if (op == spvOpFunctionCall && wc >= 4 && (i + 3) < code.size())
                        {
                            funcs[currentFunc].callees.push_back(code[i + 3]);
                            for (uint32_t w = 4; w < wc && (i + w) < code.size(); w++)
                                if (code[i + w] == samplerId)
                                    funcs[currentFunc].usesDepthSampler = true;
                        }
                    }
                    i += wc;
                }

                std::queue<uint32_t> worklist;
                std::set<uint32_t> visited;
                for (uint32_t ep : entryFuncIds)
                {
                    worklist.push(ep);
                    visited.insert(ep);
                }
                while (!worklist.empty())
                {
                    uint32_t fid = worklist.front();
                    worklist.pop();
                    auto it = funcs.find(fid);
                    if (it == funcs.end())
                        continue;
                    if (it->second.usesDepthSampler)
                        return true;
                    for (uint32_t callee : it->second.callees)
                    {
                        if (visited.insert(callee).second)
                            worklist.push(callee);
                    }
                }
                return false;
            };

            auto isSamplerUsedInSpirv = [&](uint32_t samplerId) -> bool {
                for (const auto& [entryPointName, code] : entryPointSpirv)
                    if (isSamplerUsedInOneModule(samplerId, code))
                        return true;
                return false;
            };

            for (const auto& samp : module.samplers)
            {
                if (depthTexNames.count(samp.texture_name) == 0)
                    continue;
                if (isSamplerUsedInSpirv(samp.id))
                    return true;
            }
            return false;
    }

    namespace
    {
        struct MemoryCache
        {
            std::mutex mutex;
            std::list<std::string> order;
            std::unordered_map<std::string, std::pair<std::shared_ptr<const CompiledReshadeEffect>, std::list<std::string>::iterator>> map;

            std::shared_ptr<const CompiledReshadeEffect> get(const std::string& key)
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = map.find(key);
                if (it == map.end())
                    return nullptr;
                order.erase(it->second.second);
                order.push_front(key);
                it->second.second = order.begin();
                return it->second.first;
            }

            void put(const std::string& key, std::shared_ptr<const CompiledReshadeEffect> value)
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = map.find(key);
                if (it != map.end())
                {
                    order.erase(it->second.second);
                    map.erase(it);
                }
                order.push_front(key);
                map[key] = {std::move(value), order.begin()};
                while (map.size() > MEMORY_CACHE_CAP)
                {
                    map.erase(order.back());
                    order.pop_back();
                }
            }

            void clear()
            {
                std::lock_guard<std::mutex> lock(mutex);
                map.clear();
                order.clear();
            }
        };

        MemoryCache memoryCache;

        std::shared_ptr<CompiledReshadeEffect> compileEffect(
            const std::string& fxPath,
            const std::vector<std::pair<std::string, std::string>>& macroDefinitions,
            const std::vector<std::string>& includePaths,
            bool relaxMinPrecision,
            bool liveUniforms,
            bool& cacheable)
        {
            auto e = std::make_shared<CompiledReshadeEffect>();
            cacheable = true;

            reshadefx::preprocessor preprocessor;
            addReshadeBaseMacros(preprocessor, liveUniforms);
            for (const auto& [name, value] : macroDefinitions)
                preprocessor.add_macro_definition(name, value);
            for (const auto& path : includePaths)
                preprocessor.add_include_path(path);

            if (!preprocessor.append_file(fxPath))
            {
                e->error = "Failed to load shader file";
                std::string ppErrors = preprocessor.errors();
                if (!ppErrors.empty())
                    e->error += ": " + ppErrors;
                cacheable = false;
                return e;
            }

            auto recordIncludes = [&]() {
                for (const auto& path : preprocessor.included_files())
                {
                    std::string p = path.string();
                    std::string content;
                    if (readFileBytes(p, content))
                        e->includedFiles.push_back({p, contentHash(content)});
                }
                for (const auto& resolution : preprocessor.include_resolutions())
                {
                    std::error_code ec;
                    e->includeResolutions.push_back({resolution.source, resolution.name, resolution.path,
                                                     std::filesystem::exists(std::filesystem::path(resolution.path), ec)});
                }
            };

            // reshadefx preprocessor::errors() also returns warnings, only "preprocessor error:" is fatal
            std::string ppErrors = preprocessor.errors();
            if (!ppErrors.empty() && ppErrors.find("preprocessor error:") != std::string::npos)
            {
                e->error = "Preprocessor errors: " + ppErrors;
                recordIncludes();
                return e;
            }

            e->usedMacros = preprocessor.used_macro_definitions();
            recordIncludes();

            reshadefx::parser parser;
            std::unique_ptr<reshadefx::codegen> codegen(createLayerSpirvCodegen(!liveUniforms, relaxMinPrecision));

            if (!parser.parse(preprocessor.output(), codegen.get()))
            {
                e->error = "Parse errors: " + parser.errors();
                return e;
            }

            std::string parseErrors = parser.errors();
            if (!parseErrors.empty())
                e->warnings = "Warnings: " + parseErrors;

            e->module = codegen->module();

            for (const auto& entryPoint : e->module.entry_points)
            {
                std::string binary, assembly, errors;
                if (!codegen->assemble_code_for_entry_point(entryPoint.first, binary, assembly, errors))
                {
                    e->error = "could not assemble entry point " + entryPoint.first + ": " + errors;
                    return e;
                }

                std::vector<uint32_t>& words = e->entryPointSpirv[entryPoint.first];
                words.resize(binary.size() / sizeof(uint32_t));
                std::memcpy(words.data(), binary.data(), words.size() * sizeof(uint32_t));
            }

            if (const std::string problem = entryInconsistency(*e); !problem.empty())
            {
                e->error = problem;
                return e;
            }

            e->usesDepth = moduleUsesDepth(e->module, e->entryPointSpirv);
            e->usesMinPrecision = codegen->uses_min_precision();
            return e;
        }
    }

    static std::vector<std::string> withBundledHeaders(std::vector<std::string> includePaths)
    {
        includePaths.push_back(RESHADE_INCLUDE_DIR);
        return includePaths;
    }

    std::shared_ptr<const CompiledReshadeEffect> compileReshadeEffectUncached(
        const std::string& fxPath,
        const std::vector<std::pair<std::string, std::string>>& macroDefinitions,
        const std::vector<std::string>& includePaths,
        bool relaxMinPrecision,
        bool liveUniforms)
    {
        bool cacheable = false;
        return compileEffect(fxPath, macroDefinitions, withBundledHeaders(includePaths), relaxMinPrecision, liveUniforms, cacheable);
    }

    std::shared_ptr<const CompiledReshadeEffect> getOrCompileReshadeEffect(
        const std::string& fxPath,
        const std::vector<std::pair<std::string, std::string>>& macroDefinitions,
        const std::vector<std::string>& givenIncludePaths,
        bool relaxMinPrecision,
        bool liveUniforms)
    {
        const std::vector<std::string> includePaths = withBundledHeaders(givenIncludePaths);
        std::string fxContent;
        bool haveFx = readFileBytes(fxPath, fxContent);

        std::string key;
        if (haveFx)
        {
            std::string blob;
            blob += buildIdentity();
            blob += '\x1f';
            for (const auto& [name, value] : macroDefinitions)
            {
                blob += name;
                blob += '\x1e';
                blob += value;
                blob += '\x1e';
            }
            blob += '\x1f';
            for (const auto& path : includePaths)
            {
                blob += path;
                blob += '\x1e';
            }
            blob += '\x1f';
            blob += relaxMinPrecision ? '1' : '0';
            blob += '\x1f';
            blob += liveUniforms ? '1' : '0';
            blob += '\x1f';
            blob += fxPath;
            blob += '\x1f';
            blob += fxContent;
            key = hashName(blob);

            if (auto hit = memoryCache.get(key))
            {
                if (includesUnchanged(*hit, includePaths))
                    return hit;
            }

            std::string dir = diskCacheDir();
            if (!dir.empty())
            {
                std::string path = dir + "/" + key + ".vkbfx";
                std::string payload;
                if (readFileBytes(path, payload))
                {
                    try
                    {
                        auto entry = deserializeEntry(payload);
                        if (includesUnchanged(*entry, includePaths))
                        {
                            Logger::debug("compiled effects: reused " + fxPath);
                            memoryCache.put(key, entry);
                            return entry;
                        }
                        std::remove(path.c_str());
                    }
                    catch (const std::exception& ex)
                    {
                        Logger::warn("compiled effects: discarding an unreadable entry for " + fxPath + " (" + ex.what() + ")");
                        std::remove(path.c_str());
                    }
                }
            }
        }

        if (layerLockHeld)
            Logger::warn("compiling " + fxPath + " with the layer lock held, so a frame waits for it");

        Logger::debug("compiling " + fxPath + (relaxMinPrecision ? ", half precision" : ", full precision") + (liveUniforms ? ", live uniforms" : ""));
        bool                                   cacheable = false;
        std::shared_ptr<CompiledReshadeEffect> entry;
        try
        {
            entry = compileEffect(fxPath, macroDefinitions, includePaths, relaxMinPrecision, liveUniforms, cacheable);
        }
        catch (const std::exception& ex)
        {
            entry        = std::make_shared<CompiledReshadeEffect>();
            entry->error = std::string("the shader compiler failed: ") + ex.what();
            cacheable    = false;
        }

        if (haveFx && cacheable)
        {
            memoryCache.put(key, entry);

            std::string dir = diskCacheDir();
            if (!dir.empty() && ensureCacheDir(dir))
            {
                if (writeEntryFile(dir, key, serializeEntry(*entry)))
                {
                    static std::once_flag pruneOnce;
                    std::call_once(pruneOnce, [&dir]() { pruneDiskCache(dir); });
                }
            }
        }

        return entry;
    }

    std::string compiledEffectsFolder()
    {
        return diskCacheDir();
    }

    void clearShaderCache()
    {
        memoryCache.clear();
        const std::string dir = cacheDir();
        if (dir.empty())
            return;

        size_t          removed = 0;
        std::error_code ec;
        for (const auto& item : std::filesystem::directory_iterator(dir, ec))
        {
            if (item.path().extension() != ".vkbfx" && item.path().extension() != ".tmp")
                continue;
            std::error_code removeError;
            if (std::filesystem::remove(item.path(), removeError))
                removed++;
            else if (removeError)
                Logger::warn("compiled effects: could not remove " + item.path().string() + ": " + removeError.message());
        }
        if (ec && ec != std::errc::no_such_file_or_directory)
            Logger::warn("compiled effects: could not list " + dir + ": " + ec.message());
        Logger::info("compiled effects: cleared " + std::to_string(removed) + " entries from " + dir);
    }

    std::string cacheRoundTripMismatch(const CompiledReshadeEffect& e)
    {
        const std::string blob = serializeEntry(e);
        std::shared_ptr<CompiledReshadeEffect> d;
        try
        {
            d = deserializeEntry(blob);
        }
        catch (const std::exception& ex)
        {
            return std::string("deserialize: ") + ex.what();
        }

        const auto sameType = [](const reshadefx::type& a, const reshadefx::type& b) {
            return a.base == b.base && a.rows == b.rows && a.cols == b.cols && a.qualifiers == b.qualifiers
                   && a.array_length == b.array_length && a.struct_definition == b.struct_definition;
        };
        const auto sameAnnotations = [&](const std::vector<reshadefx::annotation>& a, const std::vector<reshadefx::annotation>& b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++)
            {
                if (a[i].name != b[i].name || !sameType(a[i].type, b[i].type) || a[i].value.string_data != b[i].value.string_data
                    || std::memcmp(a[i].value.as_uint, b[i].value.as_uint, sizeof(a[i].value.as_uint)) != 0)
                    return false;
            }
            return true;
        };

        if (d->error != e.error)
            return "error";
        if (d->warnings != e.warnings)
            return "warnings";
        if (d->usesDepth != e.usesDepth)
            return "usesDepth";
        if (d->usesMinPrecision != e.usesMinPrecision)
            return "usesMinPrecision";
        if (d->usedMacros != e.usedMacros)
            return "usedMacros";
        if (d->includeResolutions != e.includeResolutions)
            return "includeResolutions";
        if (d->includedFiles != e.includedFiles)
            return "includedFiles";
        if (d->entryPointSpirv != e.entryPointSpirv)
            return "entryPointSpirv";

        const reshadefx::effect_module& a = e.module;
        const reshadefx::effect_module& b = d->module;

        if (b.entry_points.size() != a.entry_points.size())
            return "module.entry_points";
        for (size_t i = 0; i < a.entry_points.size(); i++)
            if (a.entry_points[i].first != b.entry_points[i].first || a.entry_points[i].second != b.entry_points[i].second)
                return "module.entry_points";

        if (b.textures.size() != a.textures.size())
            return "module.textures";
        for (size_t i = 0; i < a.textures.size(); i++)
        {
            const auto& x = a.textures[i];
            const auto& y = b.textures[i];
            if (x.id != y.id || x.name != y.name || x.semantic != y.semantic || x.unique_name != y.unique_name
                || x.width != y.width || x.height != y.height || x.depth != y.depth || x.levels != y.levels
                || x.type != y.type || x.format != y.format || x.render_target != y.render_target
                || x.storage_access != y.storage_access || x.semantic_binding != y.semantic_binding
                || !sameAnnotations(x.annotations, y.annotations))
                return "module.textures[" + x.unique_name + "]";
        }

        if (b.samplers.size() != a.samplers.size())
            return "module.samplers";
        for (size_t i = 0; i < a.samplers.size(); i++)
        {
            const auto& x = a.samplers[i];
            const auto& y = b.samplers[i];
            if (x.id != y.id || x.name != y.name || x.unique_name != y.unique_name || x.texture_name != y.texture_name
                || !sameType(x.type, y.type) || x.filter != y.filter || x.address_u != y.address_u
                || x.address_v != y.address_v || x.address_w != y.address_w || x.min_lod != y.min_lod
                || x.max_lod != y.max_lod || x.lod_bias != y.lod_bias || x.srgb != y.srgb
                || !sameAnnotations(x.annotations, y.annotations))
                return "module.samplers[" + x.unique_name + "]";
        }

        if (b.storages.size() != a.storages.size())
            return "module.storages";
        for (size_t i = 0; i < a.storages.size(); i++)
        {
            const auto& x = a.storages[i];
            const auto& y = b.storages[i];
            if (x.id != y.id || x.name != y.name || x.unique_name != y.unique_name || x.texture_name != y.texture_name
                || x.level != y.level || !sameType(x.type, y.type))
                return "module.storages[" + x.unique_name + "]";
        }

        const auto sameUniforms = [&](const std::vector<reshadefx::uniform>& x, const std::vector<reshadefx::uniform>& y) {
            if (x.size() != y.size())
                return false;
            for (size_t i = 0; i < x.size(); i++)
            {
                if (x[i].name != y[i].name || !sameType(x[i].type, y[i].type) || x[i].size != y[i].size
                    || x[i].offset != y[i].offset || x[i].has_initializer_value != y[i].has_initializer_value
                    || std::memcmp(x[i].initializer_value.as_uint, y[i].initializer_value.as_uint,
                                   sizeof(x[i].initializer_value.as_uint)) != 0
                    || !sameAnnotations(x[i].annotations, y[i].annotations))
                    return false;
            }
            return true;
        };
        if (!sameUniforms(a.uniforms, b.uniforms))
            return "module.uniforms";
        if (!sameUniforms(a.spec_constants, b.spec_constants))
            return "module.spec_constants";

        if (b.techniques.size() != a.techniques.size())
            return "module.techniques";
        for (size_t t = 0; t < a.techniques.size(); t++)
        {
            if (a.techniques[t].name != b.techniques[t].name
                || !sameAnnotations(a.techniques[t].annotations, b.techniques[t].annotations)
                || a.techniques[t].passes.size() != b.techniques[t].passes.size())
                return "module.techniques[" + a.techniques[t].name + "]";
            for (size_t p = 0; p < a.techniques[t].passes.size(); p++)
            {
                const auto& x = a.techniques[t].passes[p];
                const auto& y = b.techniques[t].passes[p];
                bool targetsMatch = true;
                for (int i = 0; i < 8; i++)
                    targetsMatch = targetsMatch && x.render_target_names[i] == y.render_target_names[i];
                if (!targetsMatch || x.vs_entry_point != y.vs_entry_point || x.ps_entry_point != y.ps_entry_point
                    || x.cs_entry_point != y.cs_entry_point || x.generate_mipmaps != y.generate_mipmaps
                    || x.clear_render_targets != y.clear_render_targets
                    || x.srgb_write_enable != y.srgb_write_enable || x.blend_enable[0] != y.blend_enable[0]
                    || x.stencil_enable != y.stencil_enable || x.render_target_write_mask[0] != y.render_target_write_mask[0]
                    || x.stencil_read_mask != y.stencil_read_mask || x.stencil_write_mask != y.stencil_write_mask
                    || x.color_blend_op[0] != y.color_blend_op[0] || x.alpha_blend_op[0] != y.alpha_blend_op[0]
                    || x.source_color_blend_factor[0] != y.source_color_blend_factor[0]
                    || x.dest_color_blend_factor[0] != y.dest_color_blend_factor[0]
                    || x.source_alpha_blend_factor[0] != y.source_alpha_blend_factor[0]
                    || x.dest_alpha_blend_factor[0] != y.dest_alpha_blend_factor[0]
                    || x.stencil_comparison_func != y.stencil_comparison_func
                    || x.stencil_reference_value != y.stencil_reference_value || x.stencil_pass_op != y.stencil_pass_op
                    || x.stencil_fail_op != y.stencil_fail_op || x.stencil_depth_fail_op != y.stencil_depth_fail_op
                    || x.num_vertices != y.num_vertices || x.topology != y.topology
                    || x.viewport_width != y.viewport_width || x.viewport_height != y.viewport_height
                    || x.viewport_dispatch_z != y.viewport_dispatch_z)
                    return "module.techniques[" + a.techniques[t].name + "].passes[" + std::to_string(p) + "]";
            }
        }

        if (b.total_uniform_size != a.total_uniform_size)
            return "module.total_uniform_size";

        if (serializeEntry(*d) != blob)
            return "reserialization";

        return {};
    }

}
