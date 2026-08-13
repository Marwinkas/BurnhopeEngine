#include "MainApp.hpp"
#include "Utils/SwapChain.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--no-vsync")
        {
            burnhope::BurnhopeSwapChain::setVSyncEnabled(false);
        }
    }
    burnhope::FirstApp app{};
    try
    {
        app.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}