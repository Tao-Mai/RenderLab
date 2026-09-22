#pragma once

#include "platform/input_method.h"

class Engine
{
public:
    Engine() = default;
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void init();
    void run();

private:
    bool initialized = false;
    InputMethod inputMethod;

    void mainLoop();
    void shutdown() noexcept;
};
