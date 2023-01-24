#pragma once

#include "SDL.h"

class VKlelu
{
public:
    VKlelu(int argc, char *argv[]);
    ~VKlelu();
    int run();

private:
    bool init_vulkan();

    SDL_Window *window;
};
