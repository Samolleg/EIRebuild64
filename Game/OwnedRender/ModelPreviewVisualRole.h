#pragma once

namespace OwnedRender {
// Private display objects have their own IDs. Capture the source's semantic
// role while it is known, without changing gameplay identity or saved data.
class ModelPreviewVisualRole {
    bool captured_ = false;
    bool mainHero_ = false;
public:
    bool Resolve(bool currentMainHero) const noexcept {
        return captured_ ? mainHero_ : currentMainHero;
    }
    template<class Invalidate>
    void Capture(bool sourceMainHero, Invalidate&& invalidate) {
        if (captured_ && mainHero_ == sourceMainHero) return;
        captured_ = true;
        mainHero_ = sourceMainHero;
        invalidate();
    }
};
} // namespace OwnedRender
