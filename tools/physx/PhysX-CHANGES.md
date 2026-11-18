# EIRebuild64 modifications to NVIDIA PhysX 5.13.0

The project build uses the four-argument CUDA 13 cuCtxCreate call to request a regular CUDA context with the existing flags and selected device. The private source copy also limits nvcc to two internal workers and skips GLUT binary copying when snippets are disabled. The distributed GPU library targets Turing (SM 7.5) and newer architectures.
