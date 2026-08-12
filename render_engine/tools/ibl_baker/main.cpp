// Minimal IBL baker placeholder (M5). Writes a tiny marker file for reproducibility.
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: ibl_baker <input.hdr> <out_dir>\n";
    return 2;
  }
  const std::string out_dir = argv[2];
  std::ofstream marker(out_dir + "/ibl_irradiance.marker");
  if (!marker) {
    std::cerr << "cannot write output\n";
    return 1;
  }
  marker << "ibl_baker_v0 input=" << argv[1] << "\n";
  std::ofstream pref(out_dir + "/ibl_prefilter.marker");
  pref << "ok\n";
  std::ofstream lut(out_dir + "/ibl_brdf_lut.marker");
  lut << "ok\n";
  return 0;
}
