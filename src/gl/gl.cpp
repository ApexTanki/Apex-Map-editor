#include "gl/gl.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define AE_GL_DEFINE(ret, name, params) PFN_##name name = nullptr;
AE_GL_FUNCTIONS(AE_GL_DEFINE)
#undef AE_GL_DEFINE

namespace gl {

bool load(const char** missing) {
#define AE_GL_LOAD(ret, name, params)                                              \
    name = reinterpret_cast<PFN_##name>(glfwGetProcAddress(#name));                \
    if (!name) {                                                                   \
        if (missing) *missing = #name;                                             \
        return false;                                                              \
    }
    AE_GL_FUNCTIONS(AE_GL_LOAD)
#undef AE_GL_LOAD
    return true;
}

}  // namespace gl
