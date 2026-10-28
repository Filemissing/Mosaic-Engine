//
// Materials of ray-marched primitives, see RaymarchMaterialLibrary.h
//

#include "RaymarchMaterialLibrary.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include "imgui.h"

namespace core {
    RaymarchMaterialLibrary* RaymarchMaterialLibrary::instance = nullptr;

    namespace {
        const char* MATERIAL_FOLDER = "Assets/Materials";
        const char* SURFACE_FOLDER = "Assets/shaders/raymarching/surfaces";
        const char* TEXTURE_FOLDER = "Assets/textures";
        constexpr int SNIPPET_SOURCE_BASE = 100; // #line source-string number of snippet n = 100 + n

        std::string ReadFile(const std::string& path) {
            std::ifstream stream(path);
            if (!stream.is_open()) return "";
            std::stringstream buffer;
            buffer << stream.rdbuf();
            return buffer.str();
        }

        std::vector<std::string> ListFiles(const char* folder, std::initializer_list<const char*> extensions) {
            std::vector<std::string> files;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                for (const char* e : extensions) {
                    if (ext == e) {
                        files.push_back(std::string(folder) + "/" + entry.path().filename().string());
                        break;
                    }
                }
            }
            std::sort(files.begin(), files.end());
            return files;
        }

        GLuint Compile(GLenum type, const std::string& source, std::string& log) {
            GLuint shader = glCreateShader(type);
            const char* text = source.c_str();
            glShaderSource(shader, 1, &text, nullptr);
            glCompileShader(shader);
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                GLint length = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
                log.assign(std::max(length, 1), '\0');
                glGetShaderInfoLog(shader, length, nullptr, log.data());
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        }

        // ImGui combo over a list of files, with an empty "(none)" entry
        bool FileCombo(const char* label, std::string& value, const std::vector<std::string>& files, const char* none = "(none)") {
            bool changed = false;
            if (ImGui::BeginCombo(label, value.empty() ? none : value.c_str())) {
                if (ImGui::Selectable(none, value.empty())) { value.clear(); changed = true; }
                for (const auto& f : files) {
                    if (ImGui::Selectable(f.c_str(), f == value)) { value = f; changed = true; }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    }

    RaymarchMaterialLibrary::RaymarchMaterialLibrary() {
        instance = this;
        materials.emplace_back(); // default material
        glGenBuffers(1, &materialBuffer);
        RebuildTextureArray({});
    }
    RaymarchMaterialLibrary::~RaymarchMaterialLibrary() {
        if (instance == this) instance = nullptr;
        glDeleteBuffers(1, &materialBuffer);
        if (textureArray) glDeleteTextures(1, &textureArray);
    }

    int RaymarchMaterialLibrary::IndexOf(const std::string& path) {
        if (path.empty()) return 0;
        auto it = indexOf.find(path);
        if (it != indexOf.end()) return it->second;
        return Load(path);
    }

    RaymarchMaterial* RaymarchMaterialLibrary::Get(const std::string& path) {
        return &materials[IndexOf(path)];
    }

    int RaymarchMaterialLibrary::Load(const std::string& path) {
        RaymarchMaterial material;
        if (!material.Load(path)) {
            indexOf[path] = 0; // don't retry every frame; falls back to the default material
            return 0;
        }
        int index = static_cast<int>(materials.size());
        materials.push_back(material);
        indexOf[path] = index;
        dirty = true;
        return index;
    }

    void RaymarchMaterialLibrary::Update() {
        // snippet files edited on disk -> rebuild the shader (checked once per second)
        auto now = std::chrono::steady_clock::now();
        if (now - lastSnippetCheck > std::chrono::seconds(1)) {
            lastSnippetCheck = now;
            for (const auto& s : snippets) {
                std::error_code ec;
                auto time = std::filesystem::last_write_time(s, ec);
                if (!ec && time != snippetTimes[s]) {
                    snippetTimes[s] = time;
                    shaderChanged = true;
                }
            }
        }

        if (!dirty) return;
        dirty = false;

        // resolve layered sub-materials first (this may load more materials)
        for (size_t i = 0; i < materials.size(); i++) {
            if (!materials[i].layered) continue;
            // copies: loading appends to materials, which can reallocate and invalidate references into it
            const std::string flat = materials[i].flatMaterial;
            const std::string steep = materials[i].steepMaterial;
            const std::string high = materials[i].highMaterial;
            IndexOf(flat);
            IndexOf(steep);
            if (!high.empty()) IndexOf(high);
        }

        std::vector<std::string> files;
        std::vector<std::string> newSnippets;
        auto addUnique = [](std::vector<std::string>& list, const std::string& value) {
            if (!value.empty() && std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
        };
        for (const auto& m : materials) {
            addUnique(files, m.albedoTexture);
            addUnique(files, m.normalTexture);
            addUnique(files, m.roughnessTexture);
            addUnique(newSnippets, m.surfaceShader);
        }

        if (files != textureLayers) RebuildTextureArray(files);
        if (newSnippets != snippets) {
            snippets = newSnippets;
            snippetTimes.clear();
            for (const auto& s : snippets) {
                std::error_code ec;
                snippetTimes[s] = std::filesystem::last_write_time(s, ec);
            }
            shaderChanged = true;
        }

        std::vector<GPUMaterial> gpu(materials.size());
        for (size_t i = 0; i < materials.size(); i++) {
            const RaymarchMaterial& m = materials[i];
            GPUMaterial& g = gpu[i];
            g.tint = glm::vec4(m.tint, m.normalStrength);
            g.params = glm::vec4(m.textureScale, m.blendSharpness, m.smoothness, m.metallic);
            int shaderIndex = 0;
            for (size_t s = 0; s < snippets.size(); s++) if (snippets[s] == m.surfaceShader) shaderIndex = static_cast<int>(s) + 1;
            g.layers = glm::ivec4(LayerOf(textureLayers, m.albedoTexture), LayerOf(textureLayers, m.normalTexture),
                                  LayerOf(textureLayers, m.roughnessTexture), shaderIndex);
            int flags = (m.objectSpace ? 1 : 0) | (m.layered ? 2 : 0);
            if (m.layered) {
                g.layered = glm::ivec4(IndexOf(m.flatMaterial), IndexOf(m.steepMaterial),
                                       m.highMaterial.empty() ? -1 : IndexOf(m.highMaterial), flags);
            } else {
                g.layered = glm::ivec4(-1, -1, -1, flags);
            }
            g.layerRanges = glm::vec4(m.slopeStart, m.slopeEnd, m.heightStart, m.heightEnd);
            g.extra = glm::vec4(m.layerNoise, m.textureOffset.x, m.textureOffset.y, 0.0f);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, gpu.size() * sizeof(GPUMaterial), gpu.data(), GL_DYNAMIC_DRAW);
    }

    int RaymarchMaterialLibrary::LayerOf(const std::vector<std::string>& files, const std::string& file) {
        if (file.empty()) return -1;
        auto it = std::find(files.begin(), files.end(), file);
        return it == files.end() ? -1 : static_cast<int>(it - files.begin());
    }

    void RaymarchMaterialLibrary::RebuildTextureArray(const std::vector<std::string>& files) {
        textureLayers = files;
        if (textureArray) glDeleteTextures(1, &textureArray);
        glGenTextures(1, &textureArray);
        glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray);

        const int layers = std::max(static_cast<int>(files.size()), 1); // always one layer, so the sampler is valid
        const int levels = static_cast<int>(std::floor(std::log2(TEXTURE_SIZE))) + 1;
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, GL_RGBA8, TEXTURE_SIZE, TEXTURE_SIZE, layers);

        std::vector<unsigned char> resized(static_cast<size_t>(TEXTURE_SIZE) * TEXTURE_SIZE * 4);
        for (int layer = 0; layer < layers; layer++) {
            int w = 0, h = 0, channels = 0;
            unsigned char* data = layer < static_cast<int>(files.size()) ? stbi_load(files[layer].c_str(), &w, &h, &channels, 4) : nullptr;
            if (data) {
                if (w == TEXTURE_SIZE && h == TEXTURE_SIZE) std::copy(data, data + resized.size(), resized.begin());
                else stbir_resize_uint8_linear(data, w, h, 0, resized.data(), TEXTURE_SIZE, TEXTURE_SIZE, 0, STBIR_RGBA);
                stbi_image_free(data);
                // images are stored top row first; flip so v points up (the image is upright on walls)
                const size_t row = static_cast<size_t>(TEXTURE_SIZE) * 4;
                for (int y = 0; y < TEXTURE_SIZE / 2; y++)
                    std::swap_ranges(resized.begin() + y * row, resized.begin() + (y + 1) * row,
                                     resized.begin() + (TEXTURE_SIZE - 1 - y) * row);
            } else {
                if (layer < static_cast<int>(files.size())) std::cerr << "Raymarch texture failed to load: " << files[layer] << std::endl;
                std::fill(resized.begin(), resized.end(), 255); // white
            }
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, TEXTURE_SIZE, TEXTURE_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, resized.data());
        }
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    bool RaymarchMaterialLibrary::ShaderChanged() {
        bool changed = shaderChanged;
        shaderChanged = false;
        return changed;
    }

    GLuint RaymarchMaterialLibrary::BuildMarchProgram(const std::string& vertexPath, const std::string& fragmentTemplatePath) {
        // ---- stitch: snippets at "// @SURFACE_FUNCTIONS", their switch cases at "// @SURFACE_DISPATCH" ----
        // Every snippet defines surface(); "#define surface surface_n" gives each one its own name. "#line 1 <100+n>"
        // makes compile errors in a snippet report its own line numbers, under source-string number 100 + n.
        std::istringstream templateStream(ReadFile(fragmentTemplatePath));
        std::ostringstream source;
        std::string line;
        int lineNumber = 0;
        while (std::getline(templateStream, line)) {
            lineNumber++;
            source << line << "\n";
            if (line.find("// @SURFACE_FUNCTIONS") != std::string::npos) {
                for (size_t i = 0; i < snippets.size(); i++) {
                    int n = static_cast<int>(i) + 1;
                    source << "#define surface surface_" << n << "\n";
                    source << "#line 1 " << SNIPPET_SOURCE_BASE + n << "\n";
                    source << ReadFile(snippets[i]) << "\n";
                    source << "#undef surface\n";
                }
                source << "#line " << lineNumber + 1 << " 0\n";
            } else if (line.find("// @SURFACE_DISPATCH") != std::string::npos) {
                for (size_t i = 0; i < snippets.size(); i++) {
                    int n = static_cast<int>(i) + 1;
                    source << "        case " << n << ": surface_" << n << "(i, s); break;\n";
                }
                source << "#line " << lineNumber + 1 << " 0\n";
            }
        }

        std::string log;
        GLuint vertex = Compile(GL_VERTEX_SHADER, ReadFile(vertexPath), log);
        GLuint fragment = vertex ? Compile(GL_FRAGMENT_SHADER, source.str(), log) : 0;
        if (!vertex || !fragment) {
            // name the snippet files in the log: "101(5)" -> "<snippet 1 path>(5)"
            std::string readable = log;
            for (size_t i = 0; i < snippets.size(); i++) {
                std::string tag = std::to_string(SNIPPET_SOURCE_BASE + static_cast<int>(i) + 1) + "(";
                size_t pos = 0;
                while ((pos = readable.find(tag, pos)) != std::string::npos) {
                    readable.replace(pos, tag.size(), snippets[i] + "(");
                    pos += snippets[i].size();
                }
            }
            std::cerr << "Error! March shader with surface snippets failed to compile, keeping the previous one:\n"
                      << readable << std::endl;
            if (vertex) glDeleteShader(vertex);
            return 0;
        }

        GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char info[1024];
            glGetProgramInfoLog(program, sizeof(info), nullptr, info);
            std::cerr << "Error! Linking the march shader failed, keeping the previous one:\n" << info << std::endl;
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    void RaymarchMaterialLibrary::Bind(GLuint program) const {
        glUseProgram(program);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, MATERIAL_BINDING, materialBuffer);
        glActiveTexture(GL_TEXTURE0 + TEXTURE_UNIT);
        glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray);
        GLint loc = glGetUniformLocation(program, "_MaterialTextures");
        if (loc != -1) glUniform1i(loc, static_cast<GLint>(TEXTURE_UNIT));
        loc = glGetUniformLocation(program, "materialCount");
        if (loc != -1) glUniform1i(loc, static_cast<GLint>(materials.size()));
        glActiveTexture(GL_TEXTURE0);
    }

    std::vector<std::string> RaymarchMaterialLibrary::ListMaterialFiles() {
        return ListFiles(MATERIAL_FOLDER, {".json"});
    }
    std::vector<std::string> RaymarchMaterialLibrary::ListSurfaceShaders() {
        return ListFiles(SURFACE_FOLDER, {".glsl"});
    }

    void RaymarchMaterialLibrary::DrawMaterialEditor(const std::string& path) {
        if (path.empty()) {
            ImGui::TextDisabled("Default material (create one in %s to edit it)", MATERIAL_FOLDER);
            return;
        }
        int index = IndexOf(path);
        if (index == 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Could not load %s", path.c_str());
            return;
        }
        RaymarchMaterial& m = materials[index];
        bool changed = false;

        ImGui::PushID(path.c_str());
        changed |= ImGui::Checkbox("Layered (by slope / height)", &m.layered);
        if (m.layered) {
            std::vector<std::string> materialFiles = ListMaterialFiles();
            changed |= FileCombo("Flat material", m.flatMaterial, materialFiles, "(default)");
            changed |= FileCombo("Steep material", m.steepMaterial, materialFiles, "(default)");
            changed |= FileCombo("High material", m.highMaterial, materialFiles);
            changed |= ImGui::DragFloatRange2("Slope (1 - normal.y)", &m.slopeStart, &m.slopeEnd, 0.005f, 0.0f, 1.0f);
            changed |= ImGui::DragFloatRange2("Height", &m.heightStart, &m.heightEnd, 0.1f, -500.0f, 500.0f);
            changed |= ImGui::SliderFloat("Transition noise", &m.layerNoise, 0.0f, 0.5f);
        } else {
            std::vector<std::string> textures = ListFiles(TEXTURE_FOLDER, {".png", ".jpg", ".jpeg", ".tga"});
            changed |= ImGui::ColorEdit3("Tint", &m.tint.x);
            changed |= ImGui::DragFloat("Smoothness", &m.smoothness, 0.5f, 1.0f, 512.0f);
            changed |= ImGui::SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f);
            changed |= FileCombo("Albedo", m.albedoTexture, textures);
            changed |= FileCombo("Normal map", m.normalTexture, textures);
            changed |= FileCombo("Roughness", m.roughnessTexture, textures);
            changed |= ImGui::SliderFloat("Normal strength", &m.normalStrength, 0.0f, 2.0f);
            changed |= ImGui::DragFloat("Texture scale", &m.textureScale, 0.005f, 0.001f, 10.0f);
            ImGui::SetItemTooltip("Texture repeats per unit");
            changed |= ImGui::DragFloat("Blend sharpness", &m.blendSharpness, 0.1f, 1.0f, 64.0f);
            changed |= ImGui::DragFloat2("Texture offset", &m.textureOffset.x, 0.01f);
            changed |= ImGui::Checkbox("Object space", &m.objectSpace);
            ImGui::SetItemTooltip("Project in the primitive's local space, so the texture moves with it");
        }
        changed |= FileCombo("Surface shader", m.surfaceShader, ListSurfaceShaders());
        if (ImGui::Button("Save material")) m.Save();
        ImGui::PopID();

        if (changed) dirty = true;
    }
} // core
