#pragma once
#include <algorithm>
#include <cstdlib>

namespace ModRetailShuffle {
// Retail assortment advances the engine CRT once for each growing prefix.
// It uses raw rand modulo the prefix size, not an independent uniform engine.
template<class Iterator> void Shuffle(Iterator first,Iterator last)
{
    if(first==last)return;
    for(auto current=first+1;current!=last;++current)
        std::iter_swap(current,first+std::rand()%((current-first)+1));
}
}
