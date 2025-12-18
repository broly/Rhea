#include "engine.h"
#include "common/paths.h"


int main() {
    paths::init();
    
    Engine engine;
    engine.run();
    
    return 0;
}
