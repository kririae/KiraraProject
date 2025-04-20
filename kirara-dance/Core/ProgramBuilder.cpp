#include "Core/ProgramBuilder.h"

#include "Core/DeviceUtils.h"
#include "Core/Program.h"
#include "Core/SlangContext.h"
#include "Core/SlangUtils.h"

namespace krd {
ProgramBuilder::ProgramBuilder() = default;

ProgramBuilder &ProgramBuilder::addSlangModuleFromPath(std::filesystem::path path) {
    moduleDescList.emplace_back(path);
    return *this;
}

ProgramBuilder &ProgramBuilder::addEntryPoint(std::string_view name) {
    if (moduleDescList.empty())
        throw kira::Anyhow("ProgramBuilder: No module available");
    moduleDescList.back().entryPointDescList.push_back({std::string(name)});
    return *this;
}

ProgramBuilder &ProgramBuilder::addGlobalDefine(std::string_view name, std::string_view value) {
    globalDefines.emplace_back(std::string(name), std::string(value));
    return *this;
}

Ref<Program> ProgramBuilder::link(SlangContext *context) {
    auto const device = context->getDevice();
    auto const globalSession = context->getGlobalSession();

    ComPtr<slang::ISession> slangSession;
#if 0
    slangSession = device->getSlangSession();
    if (false)
#endif
    {
        slang::SessionDesc sessionDesc{};

        // Setup target description
        slang::TargetDesc targetDesc{};
        std::string profile, targetName;

        switch (device->getDeviceInfo().deviceType) {
        case gfx::DeviceType::DirectX12:
            targetDesc.format = SLANG_DXIL;
            targetName = "KRR_D3D12";
            break;
        case gfx::DeviceType::Vulkan:
            targetDesc.format = SLANG_SPIRV;
            targetName = "KRR_VULKAN";
            break;
        case gfx::DeviceType::Metal:
            targetDesc.format = SLANG_METAL;
            targetName = "KRR_METAL";
            break;
        default:
            throw kira::Anyhow(
                "ProgramBuilder: Unsupported device type: {:s}",
                magic_enum::enum_name(device->getDeviceInfo().deviceType)
            );
        }

        if (!isShaderModelSupported(device.get(), 6, 1))
            throw kira::Anyhow("ProgramBuilder: Shader model 6.1 is not supported on the device");

        profile = std::string("sm_6_1");
        targetDesc.profile = globalSession->findProfile(profile.c_str());
        if (targetDesc.profile == SLANG_PROFILE_UNKNOWN)
            throw kira::Anyhow(
                "ProgramBuilder: Can't find Slang profile for shader model: {:s}", profile
            );

        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        // Add global defines
        kira::SmallVector<slang::PreprocessorMacroDesc> preprocessorMacros;

        {
            // Emplace the target name macro
            preprocessorMacros.emplace_back(targetName.c_str(), "1");
        }

        for (auto const &[name, value] : globalDefines)
            preprocessorMacros.emplace_back(name.c_str(), value.c_str());
        sessionDesc.preprocessorMacros = preprocessorMacros.data();
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(preprocessorMacros.size());

        slangCheck(globalSession->createSession(sessionDesc, slangSession.writeRef()));
    }

    kira::SmallVector<ComPtr<slang::IEntryPoint>> entryPoints;
    kira::SmallVector<slang::IComponentType *> componentTypes;

    for (auto const &moduleDesc : this->moduleDescList) {
        ComPtr<slang::IBlob> diagnostics;

        // The session manages the module's lifetime.
        auto *module =
            slangSession->loadModule(moduleDesc.path.string().c_str(), diagnostics.writeRef());
        slangDiagnostic(diagnostics);
        if (!module)
            throw kira::Anyhow(
                "ProgramBuilder: Failed to load the Slang module from '{}'",
                moduleDesc.path.string()
            );
        componentTypes.push_back(module);

        for (auto const &entryPointDesc : moduleDesc.entryPointDescList) {
            ComPtr<slang::IEntryPoint> slangEntryPoint;
            slangCheck(module->findEntryPointByName(
                entryPointDesc.name.c_str(), slangEntryPoint.writeRef()
            ));

            // Differently, the entry points are reference-counted, thus requires a explicit vector
            // to hold.
            componentTypes.push_back(slangEntryPoint);
            entryPoints.push_back(std::move(slangEntryPoint));
        }
    }

    ComPtr<slang::IBlob> diagnostics;
    ComPtr<slang::IComponentType> linkedProgram;
    SlangResult result = slangSession->createCompositeComponentType(
        componentTypes.data(), static_cast<SlangInt>(componentTypes.size()),
        linkedProgram.writeRef(), diagnostics.writeRef()
    );
    slangDiagnostic(diagnostics);
    slangCheck(result);

    ComPtr<gfx::IShaderProgram> shaderProgram;
    gfx::IShaderProgram::Desc shaderDesc{
        .slangGlobalScope = linkedProgram.get(),
    };
    slangCheck(device->createProgram(shaderDesc, shaderProgram.writeRef()));

    auto program = Program::create();
    program->linkedProgram = linkedProgram;
    program->shaderProgram = shaderProgram;
    return program;
}
} // namespace krd
