#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <libplacebo/shaders/colorspace.h>

namespace {
std::string transferFunction(char const* name, pl_color_transfer transfer, unsigned char id) {
    pl_shader_params params{};
    params.id = id;
    params.glsl.version = 440;
    params.glsl.vulkan = true;
    pl_shader shader = pl_shader_alloc(nullptr, &params);
    if (!shader) {
        throw std::runtime_error("Could not allocate libplacebo shader");
    }
    pl_color_space color{};
    color.primaries = PL_COLOR_PRIM_BT_709;
    color.transfer = transfer;
    // Composition is already black-normalized, with white at 1. Unknown
    // (numeric zero) would ask libplacebo to infer another display floor.
    color.hdr.min_luma = PL_COLOR_HDR_BLACK;
    color.hdr.max_luma = PL_COLOR_SDR_WHITE;
    pl_shader_delinearize(shader, &color);
    pl_shader_res const* result = pl_shader_finalize(shader);
    if (!result || result->input != PL_SHADER_SIG_COLOR || result->output != PL_SHADER_SIG_COLOR ||
        pl_shader_is_compute(shader) || result->num_vertex_attribs || result->num_variables ||
        result->num_descriptors || result->num_constants) {
        pl_shader_free(&shader);
        throw std::runtime_error("Libplacebo transfer requires unsupported shader resources");
    }
    std::string code = result->glsl;
    code += "\nvec3 " + std::string(name) + "(vec3 value) { return " + result->name + "(vec4(value, 1.0)).rgb; }\n";
    pl_shader_free(&shader);
    return code;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error("Expected input template and output shader paths");
        }
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) {
            throw std::runtime_error("Could not open compositor template");
        }
        std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad()) {
            throw std::runtime_error("Could not read compositor template");
        }
        std::string const marker = "// @LIBPLACEBO_TRANSFER_FUNCTIONS@";
        auto const offset = source.find(marker);
        if (offset == std::string::npos || source.find(marker, offset + marker.size()) != std::string::npos) {
            throw std::runtime_error("Expected exactly one libplacebo insertion marker");
        }
        std::string const functions = transferFunction("linearToSrgb", PL_COLOR_TRC_SRGB, 1) +
                                      transferFunction("linearToGamma22", PL_COLOR_TRC_GAMMA22, 2) +
                                      transferFunction("linearToPq", PL_COLOR_TRC_PQ, 3);
        source.replace(offset, marker.size(), functions);
        std::ofstream output(argv[2], std::ios::binary);
        output << source;
        output.close();
        if (!output) {
            throw std::runtime_error("Could not write generated compositor shader");
        }
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "Compositor shader generation failed: " << error.what() << '\n';
        return 1;
    }
}
