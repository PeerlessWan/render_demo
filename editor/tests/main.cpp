#include "kit_test.h"

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  return game_kit::test::RunAll(filter);
}
