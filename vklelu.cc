#include "vklelu.hh"

#include "SDL.h"
#include "SDL_vulkan.h"

#include "VkBootstrap.h"

#include "vulkan/vulkan.h"

#include <cstdio>
#include <cstdlib>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768

VKlelu::VKlelu(int argc, char *argv[]):
    window(nullptr)
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("Failed to init SDL\n");
        return;
    }

    window = SDL_CreateWindow("VKlelu",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH, WINDOW_HEIGHT,
                              SDL_WINDOW_VULKAN);
    if (!window) {
        printf("Failed to create SDL window\n");
        return;
    }

    printf("Window size: %ux%u\n", WINDOW_WIDTH, WINDOW_HEIGHT);
}

VKlelu::~VKlelu()
{
    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();
}

bool VKlelu::init_vulkan()
{
    return false;
}

int VKlelu::run()
{
    if (!window)
        return EXIT_FAILURE;

    if (!init_vulkan())
        return EXIT_FAILURE;

    bool quit = false;
    SDL_Event event;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    quit = true;
                    break;
                default:
                    break;
            }
        }

        // render();

        // swap_buffers();
    }

    return EXIT_SUCCESS;
}
