#include <iostream>
extern "C" float rust_f32(float, float);
extern "C" double rust_f64(double, double);
extern "C" float rust_calls_c32(float, float);
extern "C" double rust_calls_c64(double, double);
extern "C" float c_f32(float a, float b) { return a / b + a * b; }
extern "C" double c_f64(double a, double b) { return a / b + a * b; }
int main() {
  for (float a : {2.f, -8.f, 0.5f})
    for (float b : {2.f, 4.f, 0.5f}) {
      if (rust_f32(a, b) != c_f32(a, b) ||
          rust_calls_c32(a, b) != c_f32(a, b) ||
          rust_f64(a, b) != c_f64(a, b) || rust_calls_c64(a, b) != c_f64(a, b))
        return 1;
    }
  std::cout << "C/Rust float ABI passed in both directions\n";
}
