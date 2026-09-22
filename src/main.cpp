#include "engine.h"
#include "logger.h"

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    Engine engine;
    engine.init();
    engine.run();
}
