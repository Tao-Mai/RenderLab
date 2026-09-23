#include "engine.h"
#include "core/logger.h"

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    Engine engine;
    engine.init();
    engine.run();
}
