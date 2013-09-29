#include <stdio.h>
#include <stdlib.h>

int add(int a, int b) {
   return a+b;
}

int mul(int x, int y) {
   return x*y;
}

int main() {
   printf("%d", mul(add(1, 1), 2));
   return 0;
}

