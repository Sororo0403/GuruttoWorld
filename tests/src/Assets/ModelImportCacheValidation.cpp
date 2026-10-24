#include "ModelImportCacheValidation.h"
#include <iostream>

int main()
{
    try
    {
        ModelImportCacheValidation::Run();
        std::cout << "PASS: model import invalidation, working set reuse and LRU eviction\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
