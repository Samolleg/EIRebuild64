// EIRebuild64 integration for the pinned CUDA 13 compiler and PhysX 5.13.
// nvcc emits these references in its host-side kernel entry points. PhysX
// registers its own fat binaries and invokes CUfunctions through cuLaunchKernel.
// A direct CUDA Runtime API launch cannot use that private module registry.
extern "C" cudaError_t CUDARTAPI __cudaGetKernel(cudaKernel_t* kernel, const void*) {
    if (kernel) *kernel = nullptr;
    return cudaErrorNotSupported;
}

extern "C" cudaError_t CUDARTAPI __cudaLaunchKernel(cudaKernel_t, dim3, dim3,
                                                   void**, size_t, cudaStream_t) {
    return cudaErrorNotSupported;
}
