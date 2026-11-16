#include "image_exact_lattice.h"
#include <cassert>
#include <cstdio>
int main(){using namespace aether::rendering::software::image_exact_lattice;Context c{};const double s[]{0,0,1,1};assert(!prepare(c,s,s,s,1,1,1,true,0));std::puts("PASS no-unsigned128 selection returns legacy");}
