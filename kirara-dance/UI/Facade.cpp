#include <exception>

#include "Core/KIRA.h"
#include "Core/SlangContext.h"

int main() try {
    using namespace krd;

    SlangContext context;
    while (true)
        context.mainLoop();
} catch (std::exception const &e) { krd::LogError("{}", e.what()); }
