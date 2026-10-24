#include "../Assets/AssetFreshnessValidation.h"
#include "../Support/RegressionTimingValidation.h"
#include "../Scripts/ScriptModulePathValidation.h"
#include "DeepStateValidation.h"
#include <iostream>

int main()
{
    try
    {
        AssetFreshnessValidation::Run();
        RegressionTimingValidation::Run();
        ScriptModulePathValidation::Run();
        DeepStateValidation::GeometryFiles();
        std::cout << "PASS: asset freshness, dependency/skeleton previews, retries and CPU timing windows\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
