#include "../include/voxmask_audio.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
 vm_audio *host=nullptr;
 assert(vm_audio_create(VM_AUDIO_ABI+1,&host)!=0);
 assert(host==nullptr);
 assert(vm_audio_create(VM_AUDIO_ABI,nullptr)!=0);
 assert(vm_audio_start(nullptr)!=0);
 assert(vm_audio_stop(nullptr)!=0);
 vm_audio_metrics metrics{};
 assert(vm_audio_snapshot(nullptr,&metrics)!=0);
 vm_audio_destroy(nullptr);
 std::cout << "native ABI invalid-version and null-handle tests passed\n";
}
