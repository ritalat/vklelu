#include "vklelu.hh"

#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"

#include <cstddef>
#include <cstdio>
#include <stdexcept>

int main(int argc, char *argv[])
{
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
