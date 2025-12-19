#include "engine.h"
#include "globals.h"
#include "common/paths.h"


int main() {
    paths::init();
    
    Engine engine;
    RhGlobals::engine = &engine;
    engine.run();
    
    return 0;
}
