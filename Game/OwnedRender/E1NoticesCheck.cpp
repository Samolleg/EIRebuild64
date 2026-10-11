#include "E1Notices.h"
#include "../GameJournal.h"
#include <cstdio>
#include <limits>
#include <functional>
#include <windows.h>
#include "../HashMap.h"

namespace owned {
const char* bridge_save_id_clash_notice() noexcept;
void bridge_neighbour_diagnostic(const char*) noexcept;
}
int wmain(int argc,wchar_t** argv) {
    if (argc != 2) return 2;
    int checks=0, before=0, after=0;
    auto check=[&](bool value){if(++checks && !value)throw checks;};
    try {
        OwnedRender::E1SaveClashNotice notice;
        check(!notice.Current(0));
        check(!notice.Observe(nullptr,&after,10));
        check(!notice.Observe(&before,nullptr,10));
        check(!notice.Observe(&before,&before,10));
        check(!notice.Current(10));
        check(notice.Observe(&before,&after,10));
        check(!notice.Current(9));
        check(notice.Current(10) && notice.Current(20009));
        check(!notice.Current(20010));
        check(notice.Observe(&before,&after,20010) && notice.Current(40009));
        notice.Clear(); check(!notice.Current(20010));
        const auto nearLimit=(std::numeric_limits<std::uint64_t>::max)()-100;
        check(notice.Observe(&before,&after,nearLimit) && notice.Current(nearLimit+99));
        check(!notice.Current(0));
        GameJournal::TestStart(argv[1]);
        check(!owned::bridge_save_id_clash_notice());
        CHashMap<int,int*> objects;
        int* previous = nullptr;
        objects.Lookup(42,previous);
        OwnedRender::E1ObserveSaveReplacement(42,previous,&before);
        check(!owned::bridge_save_id_clash_notice());
        objects.SetAt(42,&before);
        objects.Lookup(42,previous);
        check(previous == &before); // Identity is the stored value, not its hash slot.
        OwnedRender::E1ObserveSaveReplacement(42,&before,&before);
        check(!owned::bridge_save_id_clash_notice());
        OwnedRender::E1ObserveSaveReplacement(42,&before,&after);
        check(owned::bridge_save_id_clash_notice()!=nullptr);
        for(int i=0;i<3;++i)owned::bridge_neighbour_diagnostic("fixture_missing_map");
        owned::bridge_neighbour_diagnostic(nullptr);
        OwnedRender::E1ClearSaveNotice();
        check(!owned::bridge_save_id_clash_notice());
        GameJournal::Stop();
        std::printf("E1 notices: %d checks PASS\n",checks); return 0;
    }catch(int failed){GameJournal::Stop();std::fprintf(stderr,"E1 notices check %d FAILED\n",failed);return 1;}
}
