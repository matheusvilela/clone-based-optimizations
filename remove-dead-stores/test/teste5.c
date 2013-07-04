#include <stdlib.h>
#include <stdio.h>

int main(int argc, char** argv) {
  int *p = (int*)malloc(sizeof(int));
  int *q = p;
  q = (int*)malloc(sizeof(int));

  if (argc) {
    *q = 0;
  } else {
    *p = 1;
  }


  int x = *p;
  printf("%d", x);
}
