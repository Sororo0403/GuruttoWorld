#include "TransformResolutionCacheValidation.h"
#include <iostream>

int main()
{
    try
    {
        TransformResolutionCacheValidation::Run();
        std::cout << "PASS: cached transforms match full resolution and reject invalid edits\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
