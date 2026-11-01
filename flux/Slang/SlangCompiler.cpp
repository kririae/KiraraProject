#include "flux/Slang/SlangCompiler.h"

#include <slang-com-ptr.h>
#include <slang.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "kira/Anyhow.h"
#include "kira/Compiler.h"

namespace flux {
namespace {
constexpr auto languageVersion = slang::CompilerOptionEntry{
    .name = slang::CompilerOptionName::LanguageVersion,
    .value = {.intValue0 = SLANG_LANGUAGE_VERSION_LEGACY},
};

[[nodiscard]] SlangCompileTarget toSlangTarget(SlangCompiler::Target target) noexcept {
    switch (target) {
    case SlangCompiler::Target::CudaSource: return SLANG_CUDA_SOURCE;
    }
    KIRA_UNREACHABLE();
}

[[nodiscard]] SlangSourceLanguage toSourceLanguage(SlangCompiler::Target target) noexcept {
    switch (target) {
    case SlangCompiler::Target::CudaSource: return SLANG_SOURCE_LANGUAGE_CUDA;
    }
    KIRA_UNREACHABLE();
}

[[nodiscard]] std::string_view toStringView(ISlangBlob *blob) noexcept {
    if (!blob)
        return {};
    return {static_cast<char const *>(blob->getBufferPointer()), blob->getBufferSize()};
}
} // namespace

struct SlangCompiler::pImpl {
    explicit pImpl(Target target) noexcept : target{target} {}

    Target target;

    /// Guards \c globalSession and every Slang object that a compile makes from it.
    std::mutex mutex;

    Slang::ComPtr<slang::IGlobalSession> globalSession;

    std::optional<std::string> prelude;

    /// \brief Returns the global session, and creates it on the first call.
    /// \pre The caller holds \c mutex.
    /// \throw kira::Anyhow If Slang fails.
    [[nodiscard]] slang::IGlobalSession &getGlobalSession() {
        if (!globalSession && SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef())))
            throw kira::Anyhow("SlangCompiler: failed to create a Slang global session");
        return *globalSession;
    }
};

SlangCompiler::SlangCompiler(Target target) : pImpl_(std::make_unique<pImpl>(target)) {}

SlangCompiler::~SlangCompiler() = default;

std::string
SlangCompiler::compile(std::span<SlangModule const> modules, std::string_view linkModule) const {
    std::scoped_lock const lock(pImpl_->mutex);

    auto &globalSession = pImpl_->getGlobalSession();

    auto const targetDescription = slang::TargetDesc{.format = toSlangTarget(pImpl_->target)};
    auto sessionDescription = slang::SessionDesc{};
    sessionDescription.targets = &targetDescription;
    sessionDescription.targetCount = 1;
    sessionDescription.compilerOptionEntries = &languageVersion;
    sessionDescription.compilerOptionEntryCount = 1;

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(globalSession.createSession(sessionDescription, session.writeRef())))
        throw kira::Anyhow("SlangCompiler: failed to create a Slang session");

    Slang::ComPtr<ISlangBlob> diagnostics;

    auto names = std::unordered_set<std::string_view>{};
    slang::IComponentType *linkTarget{};
    for (auto const &module : modules) {
        if (!names.insert(module.name).second)
            throw std::invalid_argument(
                "SlangCompiler: two modules are named '" + module.name + "'"
            );

        auto path = module.name + ".slang";
        std::ranges::replace(path, '_', '-');
        auto *loaded = session->loadModuleFromSourceString(
            module.name.c_str(), path.c_str(), module.source.c_str(), diagnostics.writeRef()
        );
        if (!loaded)
            throw kira::Anyhow(
                "SlangCompiler: Slang failed to load module '{}':\n{}", module.name,
                toStringView(diagnostics)
            );
        if (std::string_view{module.name} == linkModule)
            linkTarget = loaded;
    }

    if (!linkTarget)
        throw std::invalid_argument(
            "SlangCompiler: no module of those given is named '" + std::string{linkModule} + "'"
        );

    Slang::ComPtr<slang::IComponentType> linked;
    if (SLANG_FAILED(linkTarget->link(linked.writeRef(), diagnostics.writeRef())))
        throw kira::Anyhow(
            "SlangCompiler: Slang failed to link module '{}':\n{}", linkModule,
            toStringView(diagnostics)
        );

    Slang::ComPtr<ISlangBlob> code;
    if (SLANG_FAILED(linked->getTargetCode(0, code.writeRef(), diagnostics.writeRef())))
        throw kira::Anyhow(
            "SlangCompiler: Slang failed to generate the code of module '{}':\n{}", linkModule,
            toStringView(diagnostics)
        );

    return std::string{toStringView(code)};
}

std::string const &SlangCompiler::getTargetPrelude() const {
    std::scoped_lock const lock(pImpl_->mutex);

    if (!pImpl_->prelude) {
        Slang::ComPtr<ISlangBlob> prelude;
        auto &globalSession = pImpl_->getGlobalSession();
        globalSession.getLanguagePrelude(toSourceLanguage(pImpl_->target), prelude.writeRef());
        if (!prelude)
            throw kira::Anyhow("SlangCompiler: Slang failed to return the prelude of its target");
        pImpl_->prelude = std::string{toStringView(prelude)};
    }

    return *pImpl_->prelude;
}
} // namespace flux
