#include "EngineResourceValidation.h"
#include <iostream>

int main()
{
    try
    {
        const auto fixture = std::filesystem::absolute("generated/tests/engine-resource-runtime") /
                             (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        EngineResourceValidation::Run(fixture);
        std::cout
            << "PASS: engine resource ownership, legacy references, independent game assets and shader revisions\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
