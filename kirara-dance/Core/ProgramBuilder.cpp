#include "Core/ProgramBuilder.h"

#include "Core/Program.h"
#include "Core/SlangUtils.h"

namespace krd {
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

Ref<Program> ProgramBuilder::link(ComPtr<gfx::IDevice> const &device) {
    // Maybe we should create a custom session for the program builder.
    ComPtr<slang::ISession> slangSession;
    slangCheck(device->getSlangSession(slangSession.writeRef()));

    kira::SmallVector<ComPtr<slang::IEntryPoint>> entryPoints;
    kira::SmallVector<slang::IComponentType *> componentTypes;

    for (auto const &moduleDesc : this->moduleDescList) {
        ComPtr<slang::IBlob> diagnostics;

        // The module's lifetime is managed by the session.
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

    Ref<Program> program = new Program{};
    program->linkedProgram = linkedProgram;
    program->shaderProgram = shaderProgram;
    return program;
}
} // namespace krd
