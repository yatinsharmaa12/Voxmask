#include "../audio/processor.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <thread>
#include <random>
#include <iostream>
#include <cstdlib>
#include <new>
static thread_local bool forbid_alloc=false;
void *operator new(std::size_t size){if(forbid_alloc)std::abort();if(auto p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void *p) noexcept{std::free(p);}
void *operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void *p) noexcept{::operator delete(p);}
static void silence(const float *out,size_t n){for(size_t i=0;i<n;++i)assert(out[i]==0);}
int main(){
 using voxmask::Processor;
 float in[]={.5f,-.5f},out[]={1,1,1};
 Processor p;
 p.process(out,3,in,2,2);silence(out,3);
 assert(std::abs(p.snapshot().input_rms_db+6.020599913)<1e-6);
 p.monitor=true;p.process(out,3,in,2,2);
 assert(out[0]==.125f&&out[1]==-.125f&&out[2]==0);
 for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
  float input[]={.5f,bad};p.process(out,3,input,2,2);silence(out,3);
 }
 p.process(out,3,in,1,2);silence(out,3);
 p.process(out,3,nullptr,2,2);silence(out,3);
 p.process(out,3,in,2,0);silence(out,3);
 p.process(out,3,in,2,UINT32_MAX);silence(out,3);
 p.process(out,1,in,2,2);silence(out,1);
 p.fault.store(1);p.process(out,3,in,2,2);silence(out,3);
 Processor concurrent;std::atomic<bool> done{false};
 std::thread reader([&]{while(!done.load()){auto m=concurrent.snapshot();assert(std::isfinite(m.input_rms_db));}});
 forbid_alloc=true;
 for(int i=0;i<100000;++i)concurrent.process(nullptr,0,in,2,2);
 forbid_alloc=false;done.store(true);reader.join();assert(concurrent.snapshot().frames==200000);
 // Deterministic randomized invalid/edge counts and finite/non-finite samples.
 std::mt19937 rng(42);Processor fuzz;fuzz.monitor=true;float input[64],output[64];
 for(int trial=0;trial<100000;++trial){
  size_t available=rng()%65;uint32_t count=rng()%100;
  for(auto &x:input){uint32_t bits=rng();std::memcpy(&x,&bits,sizeof(x));}
  fuzz.process(output,64,input,available,count);
  for(float x:output)assert(std::isfinite(x)&&x>=-1&&x<=1);
  if(count==0||count>available||count>64)silence(output,64);
 }
 std::cout<<"native processor: silence, invalid input, bounds, gain, faults, no-new, concurrency and 100000 randomized blocks passed\n";
}
