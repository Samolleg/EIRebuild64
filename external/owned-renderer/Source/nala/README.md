# Nala bone runtime

UnityBodyKernel.h supplies shared affine/rig math. Model data comes from SKN
section 8 for CPU shape changes, section 9 for GPU contacts, section 13 for the
bone rig, and section 14 for rigid mounts, hands and sole alignment.

Offline model, appearance, morph, attachment and garment tables reside under
`tools/nala-to-skn/nala`. They preserve bahonec commit
`0580dff8805fc0bba33f6dce06be7f948451d4d6` and the accepted configuration.
See docs/skn-format.md for the runtime and cooking contracts.