#include <mfem.hpp>

#include "kira/Logger.h"

int main() {
    mfem::Mesh mesh("/home/krr/Projects/KiraraProject/kirara-flow/periodic-square.mesh");
    kira::LogInfo("Mesh loaded with D={}", mesh.Dimension());

    int const ndim = mesh.Dimension();
    int const ncomp = ndim + 2; // Euler equation

    mfem::DG_FECollection fec(1, ndim);
    mfem::FiniteElementSpace fespace(&mesh, &fec);
}
