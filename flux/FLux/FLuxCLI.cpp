#include "flux/FLux/FLuxCLI.h"

#include <argparse/argparse.hpp>
#include <string>
#include <utility>

#include "kira/Anyhow.h"

namespace flux {
FluxCLIRequest parseFluxCLI(int argc, char **argv) {
    argparse::ArgumentParser program{"flux", std::string{}, argparse::default_arguments::help};
    program.add_argument("scene").help("TOML scene to render").required();
    program.add_argument("-o", "--output").help("output EXR path");
    program.add_argument("--backend").help("renderer backend: optix or embree");
    program.add_argument("--spp").scan<'u', std::uint32_t>().help("override film num_samples");
    program.parse_args(argc, argv);

    FluxCLIRequest request;
    request.scenePath = program.get<std::string>("scene");
    if (auto output = program.present<std::string>("--output"))
        request.outputPath = std::filesystem::path{std::move(*output)};
    if (auto samples = program.present<std::uint32_t>("--spp")) {
        if (*samples == 0)
            throw kira::Anyhow("--spp must be greater than zero");
        request.samplesPerPixel = samples;
    }

    if (auto backend = program.present<std::string>("--backend")) {
        if (*backend == "optix")
            request.backend = RenderBackend::Optix;
        else if (*backend == "embree")
            request.backend = RenderBackend::Embree;
        else
            throw kira::Anyhow("unsupported backend '{}'", *backend);
    }

    return request;
}
} // namespace flux
