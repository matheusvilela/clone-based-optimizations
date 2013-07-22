#include <stdlib.h>
#include <stdio.h>

//Args points to two different positions,
//analysis cannot remove it
void foo(int *arg) {
  *arg = 0;
  *arg = 1;
}

int main(int argc, char** argv) {
  int *p = (int*)malloc(sizeof(int));
  int *q = (int*)malloc(sizeof(int));
  foo(q);
  foo(p);
}
