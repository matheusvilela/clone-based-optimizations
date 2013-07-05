#include <stdlib.h>
#include <stdio.h>

//Analysis cannot remove store as it's
//an argument (may be live outside function)
void foo(int *arg) {
  *arg = 1;
}

int main(int argc, char** argv) {
  int *p = (int*)malloc(sizeof(int));
  foo(p);
  printf("%d", *p);
}
