// Minimal X11 + VK_KHR_xlib_surface smoke (Mega-W11). Not the full engine.
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdint>

int main() {
  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) {
    std::printf("FAIL XOpenDisplay (set DISPLAY or use xvfb-run)\n");
    return 2;
  }
  const int screen = DefaultScreen(dpy);
  Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, 320, 240, 1,
                                   BlackPixel(dpy, screen), WhitePixel(dpy, screen));
  XStoreName(dpy, win, "x11_vk_smoke");
  XMapWindow(dpy, win);
  XFlush(dpy);

  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "x11_vk_smoke";
  app.apiVersion = VK_API_VERSION_1_1;
  const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = 2;
  ici.ppEnabledExtensionNames = exts;
  VkInstance instance = VK_NULL_HANDLE;
  VkResult r = vkCreateInstance(&ici, nullptr, &instance);
  if (r != VK_SUCCESS) {
    std::printf("FAIL vkCreateInstance %d\n", static_cast<int>(r));
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 3;
  }

  VkXlibSurfaceCreateInfoKHR sci{};
  sci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  sci.dpy = dpy;
  sci.window = win;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  r = vkCreateXlibSurfaceKHR(instance, &sci, nullptr, &surface);
  if (r != VK_SUCCESS) {
    std::printf("FAIL vkCreateXlibSurfaceKHR %d\n", static_cast<int>(r));
    vkDestroyInstance(instance, nullptr);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 4;
  }

  std::printf("OK x11_vk_smoke: XOpenDisplay + XCreateSimpleWindow + vkCreateXlibSurfaceKHR\n");
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
  XDestroyWindow(dpy, win);
  XCloseDisplay(dpy);
  return 0;
}
