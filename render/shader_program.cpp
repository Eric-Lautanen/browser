#include "shader_program.hpp"
#include <vector>

namespace browser::render {

namespace pgl = browser::platform;

ShaderProgram::ShaderProgram() = default;

ShaderProgram::~ShaderProgram() {
    if (program_id_) {
        pgl::glDeleteProgram(program_id_);
    }
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : program_id_(other.program_id_) {
    other.program_id_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        if (program_id_) pgl::glDeleteProgram(program_id_);
        program_id_ = other.program_id_;
        other.program_id_ = 0;
    }
    return *this;
}

u32 ShaderProgram::compile_shader(u32 type, const std::string& source) {
    u32 shader = pgl::glCreateShader(type);
    if (!shader) return 0;

    const char* src = source.c_str();
    pgl::glShaderSource(shader, 1, &src, nullptr);
    pgl::glCompileShader(shader);

    i32 compiled = 0;
    pgl::glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        i32 log_len = 0;
        pgl::glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        if (log_len > 0) {
            std::vector<char> log(static_cast<size_t>(log_len));
            pgl::glGetShaderInfoLog(shader, log_len, nullptr, log.data());
        }
        pgl::glDeleteShader(shader);
        return 0;
    }

    return shader;
}

Result<void> ShaderProgram::compile(const std::string& vertex_src, const std::string& fragment_src) {
    if (program_id_) {
        pgl::glDeleteProgram(program_id_);
        program_id_ = 0;
    }

    u32 vs = compile_shader(GL_VERTEX_SHADER, vertex_src);
    if (!vs) return Result<void>(std::string("Failed to compile vertex shader"));

    u32 fs = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (!fs) {
        pgl::glDeleteShader(vs);
        return Result<void>(std::string("Failed to compile fragment shader"));
    }

    u32 prog = pgl::glCreateProgram();
    if (!prog) {
        pgl::glDeleteShader(vs);
        pgl::glDeleteShader(fs);
        return Result<void>(std::string("Failed to create program"));
    }

    pgl::glAttachShader(prog, vs);
    pgl::glAttachShader(prog, fs);
    pgl::glLinkProgram(prog);

    i32 linked = 0;
    pgl::glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        i32 log_len = 0;
        pgl::glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_len);
        if (log_len > 0) {
            std::vector<char> log(static_cast<size_t>(log_len));
            pgl::glGetProgramInfoLog(prog, log_len, nullptr, log.data());
        }
        pgl::glDeleteProgram(prog);
        pgl::glDeleteShader(vs);
        pgl::glDeleteShader(fs);
        return Result<void>(std::string("Failed to link shader program"));
    }

    pgl::glDeleteShader(vs);
    pgl::glDeleteShader(fs);

    program_id_ = prog;

    // Cache uniform locations after linking
    uniforms_.projection = pgl::glGetUniformLocation(program_id_, "uProjection");
    uniforms_.texture = pgl::glGetUniformLocation(program_id_, "uTexture");
    uniforms_.use_texture = pgl::glGetUniformLocation(program_id_, "uUseTexture");

    return {};
}

void ShaderProgram::bind() const {
    pgl::glUseProgram(program_id_);
}

void ShaderProgram::unbind() const {
    pgl::glUseProgram(0);
}

i32 ShaderProgram::get_uniform_location(const std::string& name) const {
    return pgl::glGetUniformLocation(program_id_, name.c_str());
}

void ShaderProgram::set_uniform_i32(const std::string& name, i32 value) {
    // Check cached uniforms first to avoid driver round-trip
    i32 loc = -1;
    if (name == "uTexture") loc = uniforms_.texture;
    else if (name == "uUseTexture") loc = uniforms_.use_texture;
    else loc = get_uniform_location(name);
    if (loc >= 0) pgl::glUniform1i(loc, value);
}

void ShaderProgram::set_uniform_f32(const std::string& name, f32 value) {
    i32 loc = get_uniform_location(name);
    if (loc >= 0) pgl::glUniform1f(loc, value);
}

void ShaderProgram::set_uniform_mat4f(const std::string& name, const f32* matrix) {
    i32 loc = (name == "uProjection") ? uniforms_.projection : get_uniform_location(name);
    if (loc >= 0) pgl::glUniformMatrix4fv(loc, 1, GL_FALSE, matrix);
}

}
