#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <stdio.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

static int egl_error(const char *operation) {
  fprintf(stderr, "%s failed (EGL error 0x%04x)\n", operation,
          eglGetError());
  return 1;
}

int main(void) {
  EGLDisplay display =
      eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                            EGL_DEFAULT_DISPLAY, NULL);
  if (display == EGL_NO_DISPLAY)
    return egl_error("eglGetPlatformDisplay");
  if (!eglInitialize(display, NULL, NULL))
    return egl_error("eglInitialize");
  if (!eglBindAPI(EGL_OPENGL_ES_API))
    return egl_error("eglBindAPI");

  const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_NONE,
  };
  EGLConfig config = NULL;
  EGLint config_count = 0;
  if (!eglChooseConfig(display, config_attributes, &config, 1,
                       &config_count) || config_count != 1)
    return egl_error("eglChooseConfig");

  const EGLint surface_attributes[] = {
      EGL_WIDTH, 4, EGL_HEIGHT, 4, EGL_NONE,
  };
  EGLSurface surface =
      eglCreatePbufferSurface(display, config, surface_attributes);
  if (surface == EGL_NO_SURFACE)
    return egl_error("eglCreatePbufferSurface");

  const EGLint context_attributes[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE,
  };
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                                        context_attributes);
  if (context == EGL_NO_CONTEXT)
    return egl_error("eglCreateContext");
  if (!eglMakeCurrent(display, surface, surface, context))
    return egl_error("eglMakeCurrent");

  glClearColor(1.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  GLubyte pixel[4] = {0, 0, 0, 0};
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
  if (glGetError() != GL_NO_ERROR || pixel[0] < 250 || pixel[1] > 5 ||
      pixel[2] > 5 || pixel[3] < 250) {
    fprintf(stderr, "unexpected softpipe pixel: %u,%u,%u,%u\n", pixel[0],
            pixel[1], pixel[2], pixel[3]);
    return 1;
  }

  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglDestroySurface(display, surface);
  eglTerminate(display);
  puts("ARMv7 Mesa softpipe EGL/GLES2 rendering: OK");
  return 0;
}
