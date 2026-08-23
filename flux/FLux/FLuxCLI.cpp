#include "flux/FLux/FLuxCLI.h"

#include <argparse/argparse.hpp>
#include <filesystem>
#include <string>
#include <utility>

#include "kira/Anyhow.h"
#include "kira/Logger.h"

namespace flux {
FluxCLIRequest parseFluxCLI(int argc, char **argv) {
    argparse::ArgumentParser program{"flux", std::string{}, argparse::default_arguments::help};
    program.add_argument("scene").help("TOML scene file").required();
    program.add_argument("-o", "--output").help("write the color image to this EXR file");
    program.add_argument("--backend").help("render with 'optix' or 'embree'");
    program.add_argument("--spp").scan<'u', std::uint32_t>().help("override samples per pixel");
    program.add_argument("--log-level")
        .metavar("LEVEL")
        .help(
            "show logs at this level or higher: 'trace', 'debug', 'info', 'warning', or 'error'; "
            "default: 'warning'"
        );
    program.add_argument("--log-file").help("also append logs to this file");
    program.parse_args(argc, argv);

    FluxCLIRequest request;
    request.scenePath = program.get<std::string>("scene");
    if (auto output = program.present<std::string>("--output"))
        request.outputPath = std::filesystem::path{std::move(*output)};
    if (auto path = program.present<std::string>("--log-file"))
        request.logFilePath = std::filesystem::path{std::move(*path)};
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
            throw kira::Anyhow("backend must be 'optix' or 'embree', got '{}'", *backend);
    }

    if (auto level = program.present<std::string>("--log-level")) {
        if (*level == "trace")
            request.logLevel = spdlog::level::trace;
        else if (*level == "debug")
            request.logLevel = spdlog::level::debug;
        else if (*level == "info")
            request.logLevel = spdlog::level::info;
        else if (*level == "warning")
            request.logLevel = spdlog::level::warn;
        else if (*level == "error")
            request.logLevel = spdlog::level::err;
        else
            throw kira::Anyhow(
                "log level must be 'trace', 'debug', 'info', 'warning', or 'error', got '{}'",
                *level
            );
    }

    return request;
}

void configureLogging(FluxCLIRequest const &request) {
    if (request.logFilePath) {
        auto const &path = *request.logFilePath;
        if (auto const &parent = path.parent_path(); !parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error)
                throw kira::Anyhow(
                    "failed to create log directory '{}': {}", parent.string(), error.message()
                );
        }
    }

    kira::LoggerBuilder builder{"flux"};
    (void)builder.filter_level(request.logLevel);
    if (request.logFilePath)
        (void)builder.to_file(*request.logFilePath);
    auto logger = builder.init();
    logger->flush_on(spdlog::level::warn);
}
} // namespace flux
