#include <stdlib.h>
#include <stdio.h>

int *a;

void foo() {
  int *b = (int*)malloc(sizeof(int));
  b = a;
  *b = 5;
}

int main() {
  a = (int*)malloc(sizeof(int));
  printf("%d", *a);
}
