// Lets the engine-independent core load as an Unreal module. Compiles to nothing outside Unreal.
#if __has_include("Modules/ModuleManager.h")
#include "Modules/ModuleManager.h"
IMPLEMENT_MODULE(FDefaultModuleImpl, GolfCore)
#endif
