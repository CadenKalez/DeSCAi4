#include <cstdio>
#include <thread>
#include <chrono>

#include "gz/MessageQueue.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

int main() {
    auto* q = new cGZMessageQueue();
    q->AddRef();
    void* out = nullptr;
    CHECK(q->QueryInterface(kGZIID_cIGZMessageQueue, &out) && out == static_cast<cIGZMessageQueue*>(q));
    q->Release();
    CHECK(!q->QueryInterface(0x1234, &out));

    CHECK(q->GetMessageCount() == 0 && q->PeekFrontType() == 0 && q->PeekBackMessage() == nullptr && q->GetFrontType() == 0);
    cGZMessage a{5, 1, 2, 3}, b{6, 4, 5, 6}, c{5, 9, 9, 9};
    q->PushBack(a); q->PushBack(b); q->PushFront(c);
    CHECK(q->GetMessageCount() == 3 && q->PeekFrontType() == 5 && q->PeekBackType() == 6);
    CHECK(q->PeekFrontMessage()->param1 == 9 && q->PeekBackMessage()->param1 == 4);
    CHECK(q->PeekAnywhereType(6) && !q->PeekAnywhereType(7) && q->PeekAnywhereMessage(a) && !q->PeekAnywhereMessage({5, 1, 2, 4}));
    CHECK(!q->RemoveLastFound(a) && !q->RemoveLastFound(5u) && q->GetMessageCount() == 3);   // never implemented
    CHECK(q->RemoveAllFound(5u) == 2 && q->GetMessageCount() == 1);
    q->PushBack(a); q->PushBack(a);
    CHECK(q->RemoveAllFound(a) == 2 && q->RemoveFirstFound(b) && !q->RemoveFirstFound(b) && q->GetMessageCount() == 0);
    q->PushBack(a); q->PushBack(b); q->PushBack(c);
    cGZMessage got;
    CHECK(q->GetBackMessage(got) && got == c && q->GetFrontMessage(got) && got == a && q->GetFrontType() == 6 && !q->GetFrontMessage(got));
    q->PushBack(a); q->PushBack(b);
    CHECK(q->GetAnywhereType(6) && q->GetAnywhereMessage(a) && q->GetMessageCount() == 0);
    q->PushBack(a); q->PushBack(b);
    CHECK(q->RemoveFirstFound(5u) && q->GetBackType() == 6 && q->RemoveAll() == 0);
    q->PushBack(a); q->PushBack(b);
    CHECK(q->RemoveAll() == 2);

    std::thread producer([q] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); q->PushBack({42, 7, 0, 0}); });
    cGZMessage blocked;
    CHECK(q->GetFrontMessageBlock(blocked) && blocked.type == 42 && blocked.param1 == 7);
    producer.join();
    std::thread producer2([q] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); q->PushFront({43, 0, 0, 0}); });
    CHECK(q->GetBackTypeBlock() == 43);
    producer2.join();
    q->Release();
    std::printf(failures ? "FAILED\n" : "gz_messagequeue ok\n");
    return failures != 0;
}
