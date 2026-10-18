#include <cutenes/version.hpp>

namespace cutenes {

const char* coreVersionString()
{
    // Kept in one translation unit so the save-state codec and any consumer
    // agree on a single instance.
    return "0.1.0";
}

} // namespace cutenes
