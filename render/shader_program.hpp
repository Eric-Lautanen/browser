#pragma once
#include <string>
#include "../tests/utility.hpp"
#include "../platform/opengl.hpp"

namespace browser::render {

class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    Result<void> compile(const std::string& vertex_src, const std::string& fragment_src);
    void bind() const;
    void unbind() const;
    u32 id() const { return program_id_; }

    i32 get_uniform_location(const std::string& name) const;
    void set_uniform_i32(const std::string& name, i32 value);
    void set_uniform_f32(const std::string& name, f32 value);
    void set_uniform_mat4f(const std::string& name, const f32* matrix);

    // Cached uniform locations for the default renderer shader
    struct Uniforms {
        i32 projection = -1;
        i32 texture = -1;
        i32 use_texture = -1;
        i32 texture_is_rgba = -1;
        i32 use_sdf = -1;
    };
    const Uniforms& uniforms() const { return uniforms_; }

private:
    u32 program_id_ = 0;
    Uniforms uniforms_;
    static u32 compile_shader(u32 type, const std::string& source);
};

}
