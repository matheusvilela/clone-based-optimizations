#include <stdlib.h>
#include <stdio.h>

void foo(int* a, int *b) {
   *a = 5;
   *b = 7;
}

int* bar() {
  int *a = (int*)malloc(sizeof(int));
  int *b = (int*)malloc(sizeof(int));
  int *c = (int*)malloc(sizeof(int));
  foo(a, b);
  foo(b, c);
  printf("%d, %d", *a, *c);
  return c;
}


int main(int argc, char** argv) {
   bar();
}
