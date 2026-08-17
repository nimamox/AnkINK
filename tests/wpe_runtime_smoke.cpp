#include <wpe/unstable/fdo-shm.h>
#include <wpe/wpe.h>

#include <dlfcn.h>
#include <unistd.h>

#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2 || argv[1][0] != '/') {
    std::cerr << "usage: ankink_wpe_runtime_smoke /absolute/backend.so\n";
    return 2;
  }
  void *backend = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!backend) {
    std::cerr << "dlopen failed: " << dlerror() << '\n';
    return 1;
  }
  auto *loader = static_cast<wpe_loader_interface *>(
      dlsym(backend, "_wpe_loader_interface"));
  if (!loader) {
    std::cerr << "backend has no _wpe_loader_interface: " << dlerror() << '\n';
    return 1;
  }
  std::cerr << "backend loader interface=" << static_cast<void *>(loader)
            << " load_object=" << reinterpret_cast<void *>(loader->load_object)
            << '\n';
  if (!loader->load_object) {
    std::cerr << "backend loader has a null load_object callback\n";
    return 1;
  }
  void *renderer_host = loader->load_object("_wpe_renderer_host_interface");
  std::cerr << "direct renderer host interface=" << renderer_host << '\n';
  if (!renderer_host) {
    std::cerr << "backend did not provide its renderer host interface\n";
    return 1;
  }

  if (!wpe_loader_init(argv[1])) {
    std::cerr << "wpe_loader_init failed\n";
    return 1;
  }
  if (!wpe_fdo_initialize_shm()) {
    std::cerr << "wpe_fdo_initialize_shm failed\n";
    return 1;
  }

  // This is the exact libwpe lookup that previously aborted on the Kindle.
  const int renderer_fd = wpe_renderer_host_create_client();
  if (renderer_fd < 0) {
    std::cerr << "renderer host client creation failed\n";
    return 1;
  }
  close(renderer_fd);
  std::cout << "WPE loader and SHM renderer host: OK\n";
  return 0;
}
