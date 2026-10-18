#include <cstdio>

#include "gz/cGZMessageQueueManager2.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    auto* mgr = new cGZMessageQueueManager2();
    mgr->AddRef();
    CHECK(mgr->GetServiceID() == kGZSID_cGZMessageQueueManager2 && mgr->GetServicePriority() == 5000000);
    CHECK(mgr->Init() && mgr->Shutdown());
    void* raw = nullptr;
    CHECK(mgr->QueryInterface(kGZIID_cIGZMessageQueueManager2, &raw) && raw == static_cast<cIGZMessageQueueManager2*>(mgr));
    mgr->Release();
    CHECK(mgr->QueryInterface(kGZIID_cIGZSystemService, &raw));
    mgr->Release();
    CHECK(!mgr->QueryInterface(0x1234, &raw));

    cIGZMessageQueue2* q = reinterpret_cast<cIGZMessageQueue2*>(0x1);
    CHECK(mgr->GetQueueCount() == 0 && !mgr->GetQueue(5, &q) && q == nullptr);
    CHECK(mgr->AddQueue(5) && !mgr->AddQueue(5) && mgr->GetQueueCount() == 1);
    CHECK(mgr->GetQueue(5, &q) && q);
    cIGZMessageQueue2* same = nullptr;
    CHECK(mgr->GetQueue(5, &same) && same == q);
    same->Release();

    auto* mine = new cGZMessageQueue2();
    mine->AddRef();
    CHECK(mgr->AddQueue(9, mine) && !mgr->AddQueue(9, mine) && !mgr->AddQueue(5, mine));
    CHECK(mgr->GetQueueCount() == 2);
    CHECK(mgr->RemoveQueue(mine) && !mgr->RemoveQueue(mine) && mgr->GetQueueCount() == 1);
    mine->Release();   // the manager dropped its reference; this was the last
    CHECK(mgr->RemoveQueue(5) && !mgr->RemoveQueue(5));
    q->Release();
    CHECK(mgr->AddQueue(1) && mgr->AddQueue(2) && mgr->AddQueue(3) && mgr->GetQueueCount() == 3);
    CHECK(mgr->RemoveAllQueues() && mgr->GetQueueCount() == 0);
    CHECK(mgr->AddQueue(7, nullptr) && mgr->GetQueueCount() == 1);   // a null queue is stored
    q = nullptr;
    CHECK(!mgr->GetQueue(8, &q));
    mgr->Release();
    std::printf(failures ? "FAILED\n" : "gz_messagequeuemanager2 ok\n");
    return failures != 0;
}
