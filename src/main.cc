#include "vklelu.hh"

#include "SDL.h"

#include <cstddef>
#include <cstdio>
#include <stdexcept>

int main(int argc, char *argv[])
{
    fprintf(stderr, "Launching VKlelu\n");

    SDL_version compiled;
    SDL_version linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    fprintf(stderr, "Compiled with: SDL %u.%u.%u\n",
            compiled.major, compiled.minor, compiled.patch);
    fprintf(stderr, "Loaded: SDL %u.%u.%u\n",
            linked.major, linked.minor, linked.patch);

    try {
        VKlelu vklelu(argc, argv);
        return vklelu.run();
    } catch (std::runtime_error& e) {
        fprintf(stderr, "Unhandled exception: %s\n", e.what());
#if defined(WIN32) && defined(NDEBUG)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unhandled exception!", e.what(), NULL);
#endif
        return EXIT_FAILURE;
    }
}
