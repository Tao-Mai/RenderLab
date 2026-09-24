#pragma once

#include "platform/InputMethod.h"

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
    bool inited = false;
    InputMethod inputMethod;

    void mainLoop();
    void shutdown() noexcept;
};
