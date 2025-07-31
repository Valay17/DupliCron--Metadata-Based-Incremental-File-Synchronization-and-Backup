#include "ConfigGlobal.hpp"
#include "ControlFlow.hpp"

int main()
{
    ConfigGlobal::InitializeDefaults();

    ControlFlow App;
    return App.Run();
}