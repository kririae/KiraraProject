#include "Core/KIRA.h"
#include "Core/Math.h"

int main() {
    using namespace krd;
    Eigen::Matrix3d mat3d = Eigen::Matrix3d::Identity();

    krd::LogInfo("Hello, KiraraDance2! {}", mat3d);
}
