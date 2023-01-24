#include "vklelu.hh"

#include "SDL.h"

#include <cstdio>

int main(int argc, char *argv[])
{
    SDL_version compiled;
    SDL_version linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    printf("Compiled with: SDL %u.%u.%u\n",
           compiled.major, compiled.minor, compiled.patch);
    printf("Loaded: SDL %u.%u.%u\n",
           linked.major, linked.minor, linked.patch);

    VKlelu vklelu(argc, argv);
    return vklelu.run();
}
