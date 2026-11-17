#include <cstdio>
#include <cstring>
#include "pge/version.h"

#define PGE_CHECK(cond) \
  do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

int main() {
  PGE_CHECK(std::strcmp(pge_version(), "0.1.1") == 0);
  PGE_CHECK(PGE_VERSION_MAJOR == 0 && PGE_VERSION_MINOR == 1 && PGE_VERSION_PATCH == 1);
  return 0;
}
