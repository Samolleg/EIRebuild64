# Nala bone runtime

The game retains UnityBodyKernel.h and the accepted size/sole constants.
Section 13 supplies the rig/controller payload. Runtime evaluates bones only;
SKN vertices and section-9 contacts are evaluated by the native GPU shader.

Offline model, appearance, morph and garment headers are now under
`tools/nala-to-skn/nala`. They preserve bahonec commit
`0580dff8805fc0bba33f6dce06be7f948451d4d6` and the accepted configuration.
The former nala_runtime.cpp CPU geometry submission has been removed.
See docs/skn-format.md for implemented behavior and remaining limitations.
