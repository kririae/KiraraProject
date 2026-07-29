#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Integrator/PathIntegratorImpl.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace {
class ThrowingIntegratorOwner final : public flux::RenderObject {
    friend class flux::TXContext;

    ThrowingIntegratorOwner(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {
        (void)tx.create<flux::PathIntegrator>();
        throw std::runtime_error("intentional integrator transaction failure");
    }
};
} // namespace

TEST(PathIntegratorTests, KeepsFirstSuccessfulIntegratorActive) {
    auto context = flux::Context::create();

    EXPECT_THROW((void)context->getActiveIntegrator(), kira::Anyhow);
    EXPECT_THROW((void)context->create<ThrowingIntegratorOwner>(), std::runtime_error);
    EXPECT_THROW((void)context->getActiveIntegrator(), kira::Anyhow);

    auto first = context->create<flux::PathIntegrator>();
    EXPECT_EQ(context->getActiveIntegrator().get(), first.get());

    (void)context->create<flux::PathIntegrator>();
    EXPECT_EQ(context->getActiveIntegrator().get(), first.get());
}

TEST(PathIntegratorTests, TerminatesCompletedPathsOnHost) {
    auto state = flux::PathState{};

    flux::PathIntegrator::Impl{}.onMiss(state);

    EXPECT_FALSE(state.active);
}
