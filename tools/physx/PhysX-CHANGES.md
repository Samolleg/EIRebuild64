# EIRebuild64 modifications to NVIDIA PhysX 5.13.0

The project build uses the four-argument CUDA 13 cuCtxCreate call to request a regular CUDA context with the existing flags and selected device. The private source copy also limits nvcc to two internal workers and skips GLUT binary copying when snippets are disabled. The distributed GPU library targets Turing (SM 7.5) and newer architectures.

PhysX registers its own fat binaries and dispatches kernels through the CUDA Driver API. The upstream registration layer also supplies CUDA host launch symbols referenced by nvcc. EIRebuild64 adds the two CUDA 13 host entry points and reports a direct runtime launch as unsupported. PhysX simulation dispatches through cuLaunchKernel.
