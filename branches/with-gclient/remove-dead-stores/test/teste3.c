#include <stdlib.h>
#include <stdio.h>

int *a;

int main() {
  int *b = (int*)malloc(sizeof(int));
  a = b;
  *b = 5;
}
