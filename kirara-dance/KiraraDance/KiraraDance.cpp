#include <exception>

#include "Core/SlangContext.h"

int main() try {
    using namespace krd;

    auto window = Window::create(Window::Desc{800, 600, "Kirara Dance"});
    SlangGraphicsContext::Desc desc{};

    auto context = SlangGraphicsContext::create(desc, window);
} catch (std::exception const &e) { krd::LogError("{}", e.what()); }
