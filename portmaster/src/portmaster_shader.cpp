#include <vitaGL.h>

#include <ctype.h>
#include <sstream>
#include <string>

namespace {

// The renderer emits Cg with a single entry point and scalar/vector parameters.
// Preserve that interface while moving the parameters to GLSL global scope.
void replace(std::string &text, const std::string &from, const std::string &to)
{
    std::string::size_type offset = 0;
    while ((offset = text.find(from, offset)) != std::string::npos) {
        text.replace(offset, from.size(), to);
        offset += to.size();
    }
}

std::string translate_tokens(const std::string &text)
{
    std::string result;
    for (std::string::size_type i = 0; i < text.size();) {
        if (isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
            const std::string::size_type begin = i++;
            while (i < text.size() &&
                   (isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
                ++i;
            const std::string token = text.substr(begin, i - begin);
            if (token == "tex2D") result += "texture2D";
            else if (token == "lerp") result += "mix";
            else if (token == "rsqrt") result += "inversesqrt";
            else if (token == "half") result += "float";
            else if (token.size() == 6 && token.compare(0, 5, "float") == 0)
                result += "vec" + token.substr(5);
            else if (token.size() == 5 && token.compare(0, 4, "half") == 0)
                result += "vec" + token.substr(4);
            else if (token.size() == 4 && token.compare(0, 3, "int") == 0)
                result += "ivec" + token.substr(3);
            else
                result += token;
        } else if (isdigit(static_cast<unsigned char>(text[i]))) {
            char *end;
            const char *begin = text.c_str() + i;
            strtod(begin, &end);
            result.append(begin, end - begin);
            i += end - begin;
            if (i < text.size() && text[i] == 'f')
                ++i;
        } else {
            result += text[i++];
        }
    }
    return result;
}

} // namespace

int portmaster_shader_source(GLuint shader, GLenum type, const char *source, const char *name)
{
    const bool vertex = type == GL_CG_VERTEX_SHADER_EXT;
    const bool shadow_receiver = strcmp(name, "ombre reception") == 0;
    std::string cg(source);
    const std::string signature = vertex ? "void main(" : "float4 main(";
    const std::string::size_type start = cg.find(signature);
    if (start == std::string::npos)
        return false;
    const std::string::size_type args = start + signature.size();
    const std::string::size_type end = cg.find(')', args);
    if (end == std::string::npos)
        return false;

    std::string declarations;
    std::istringstream parameters(cg.substr(args, end - args));
    std::string parameter;
    while (std::getline(parameters, parameter, ',')) {
        std::istringstream fields(parameter);
        std::string value_type, identifier, colon, semantic;
        fields >> value_type >> identifier;
        if (value_type == "uniform") {
            fields >> colon;
            if (identifier.empty() || colon.empty())
                return false;
            declarations += "uniform " + identifier + " " + colon + ";\n";
            continue;
        }
        const bool output = identifier == "out";
        if (output)
            fields >> identifier;
        fields >> colon >> semantic;
        if (identifier.empty() || (output && semantic.empty()))
            return false;
        if (semantic == "POSITION")
            declarations += "#define " + identifier + " gl_Position\n";
        else if (semantic == "CLP0" && shadow_receiver)
            declarations += "varying vec4 vClip;\n";
        else if (output || !vertex)
            declarations += "varying " + value_type + " " + identifier + ";\n";
        else
            declarations += "attribute " + value_type + " " + identifier + ";\n";
    }

    std::string body = cg.substr(0, start) +
        (vertex ? "void main()" : "float4 shade()") + cg.substr(end + 1);
    // Cg's half casts only narrow precision. GLSL precision is selected by
    // the GLES driver. Bone indices still require explicit integer conversion.
    replace(body, "(half)", "");
    replace(body, "(half3)", "");
    replace(body, "(int3)min(aOs, float3(63.f, 63.f, 63.f))",
                  "int3(min(aOs, float3(63.f, 63.f, 63.f)))");
    std::string glsl =
        "#version 120\n"
        "float saturate(float x) { return clamp(x, 0.0, 1.0); }\n"
        "vec2 saturate(vec2 x) { return clamp(x, 0.0, 1.0); }\n"
        "vec3 saturate(vec3 x) { return clamp(x, 0.0, 1.0); }\n"
        "vec4 saturate(vec4 x) { return clamp(x, 0.0, 1.0); }\n";
    if (!vertex)
        glsl += "void clip(float x) { if (x < 0.0) discard; }\n";
    glsl += translate_tokens(declarations + body);
    if (!vertex) {
        if (shadow_receiver)
            glsl += "varying vec4 vClip;\n"
                    "\n\nvoid main()\n{\n"
                    "    if (any(lessThan(vClip, vec4(0.0)))) discard;\n"
                    "    gl_FragColor = shade();\n}\n";
        else
            glsl += "\n\nvoid main()\n{\n    gl_FragColor = shade();\n}\n";
    }
    const char *translated = glsl.c_str();
    glShaderSource(shader, 1, &translated, NULL);
    return true;
}
