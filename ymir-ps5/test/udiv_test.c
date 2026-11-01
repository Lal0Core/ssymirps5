#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#define __udivti3 my_udivti3
#define _umtx_op my_umtx_op
#include "../tooling/ymir/ps5_core_shims.c"
static uint64_t r64(void){ return ((uint64_t)rand()<<62) ^ ((uint64_t)rand()<<31) ^ rand(); }
int main(void){
  srand(1); long bad=0;
  for (int i=0;i<2000000;i++){
    unsigned __int128 n=((unsigned __int128)r64()<<64)|r64(), d;
    switch(i%4){case 0:d=((unsigned __int128)r64()<<64)|r64();break;case 1:d=r64();break;case 2:d=(r64()>>(rand()%64))|1;break;default:d=((unsigned __int128)(r64()>>(rand()%64))<<64)|r64();}
    if(i%7==0) n>>= rand()%128;
    if(!d) d=1;
    if (my_udivti3(n,d)!=n/d) bad++;
  }
  printf("udivti3 mismatches: %ld\n", bad); return bad!=0;
}
