#pragma once
#include "dom/dom_lifetime_testing.h"
#if defined(AETHER_DOM_LIFETIME_TESTING)
// Declare before any fixture/oracle/retained handles. Its destructor executes
// after those owners and must run only after finalizer/helper threads are joined.
struct DomLifetimeCheck {
    aether::dom::lifetime_test::Snapshot before=aether::dom::lifetime_test::snapshot();
    DomLifetimeCheck(){CHECK(before.valid);}
    ~DomLifetimeCheck(){
        const auto after=aether::dom::lifetime_test::snapshot();
        if(!(before==after)){
            for(unsigned i=0;i<aether::dom::lifetime_test::family_count;++i)
                std::fprintf(stderr,"lifetime family %u before=%llu after=%llu valid=%d\n",i,
                    (unsigned long long)before.live[i],(unsigned long long)after.live[i],int(after.valid));
        }
        CHECK(after.valid&&before==after);
    }
};
#else
struct DomLifetimeCheck {};
#endif
