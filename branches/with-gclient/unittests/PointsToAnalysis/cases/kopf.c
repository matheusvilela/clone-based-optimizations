#include <stdio.h>

int main() {
  int a, b, *c, *d;

  int* w = &a;
  int* x = &b;
  int** y = &c;
  int** z = y;
  c = 0;
  *y = w;
  *z = x;
  y = &d;
  z = y;
  *y = w;
  *z = x;
//  print("%d, %d, %d, %d\n", *w, *x, **y, **z);
}
