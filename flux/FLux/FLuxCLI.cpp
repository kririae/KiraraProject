#include "flux/FLux/FLuxCLI.h"

#include <argparse/argparse.hpp>
#include <filesystem>
#include <string>
#include <utility>

#include "kira/Anyhow.h"
#include "kira/Logger.h"

namespace flux {
FluxCLIOptions parseFluxCLI(int argc, char **argv) {
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
    program.add_argument("--launch-waves")
        .scan<'g', double>()
        .help(
            "OptiX megakernel threads as a multiple of the estimated resident thread count; "
            "lanes claim the rest, default: 2"
        );
    program.add_argument("--chunk-size")
        .scan<'u', std::uint32_t>()
        .help("consecutive paths an OptiX thread renders in order, clamps to [1, --spp]");
    program.add_argument("--log-file").help("also append logs to this file");
    program.parse_args(argc, argv);

    FluxCLIOptions options;
    options.scenePath = program.get<std::string>("scene");
    if (auto output = program.present<std::string>("--output"))
        options.outputPath = std::filesystem::path{std::move(*output)};
    if (auto path = program.present<std::string>("--log-file"))
        options.logFilePath = std::filesystem::path{std::move(*path)};
    options.samplesPerPixel = program.present<std::uint32_t>("--spp");

    if (auto waves = program.present<double>("--launch-waves")) {
        if (!(*waves > 0.0))
            throw kira::Anyhow("--launch-waves must be greater than zero");
        options.launchWaves = waves;
    }

    options.chunkSize = program.present<std::uint32_t>("--chunk-size");

    if (auto backend = program.present<std::string>("--backend")) {
        if (*backend == "optix")
            options.backend = RenderBackend::Optix;
        else if (*backend == "embree")
            options.backend = RenderBackend::Embree;
        else
            throw kira::Anyhow("backend must be 'optix' or 'embree', got '{}'", *backend);
    }

    if (auto level = program.present<std::string>("--log-level")) {
        if (*level == "trace")
            options.logLevel = spdlog::level::trace;
        else if (*level == "debug")
            options.logLevel = spdlog::level::debug;
        else if (*level == "info")
            options.logLevel = spdlog::level::info;
        else if (*level == "warning")
            options.logLevel = spdlog::level::warn;
        else if (*level == "error")
            options.logLevel = spdlog::level::err;
        else
            throw kira::Anyhow(
                "log level must be 'trace', 'debug', 'info', 'warning', or 'error', got '{}'",
                *level
            );
    }

    return options;
}

void configureLogging(FluxCLIOptions const &options) {
    if (options.logFilePath) {
        auto const &path = *options.logFilePath;
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
    (void)builder.filter_level(options.logLevel);
    if (options.logFilePath)
        (void)builder.to_file(*options.logFilePath);
    auto logger = builder.init();
    logger->flush_on(spdlog::level::warn);
}
} // namespace flux
