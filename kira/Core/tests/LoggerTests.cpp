#include "kira/Logger.h"

int main() {
  kira::LogInfo("qwq {}", 1);
  kira::LogInfo<"kira1">("qwq {}", 1);
  kira::LogInfo<"kira2">("qwq {}", 2);
  kira::LogInfo<"kira3">("qwq {}", 3);
}
