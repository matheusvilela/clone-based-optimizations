#include <stdio.h>
#include <stdlib.h>

int sumloop(int a, int b, int c) {
  int sum = 0;
  for (int i = 0; i < c; i++)
    sum += a * b;

  return sum;
}
int main() {
  for (int i = 0; i < 10000000; i++) {
    int result = sumloop(1, 2, 3);
    printf("%d", result);
     result = sumloop(3, 4, 5);
    printf("%d", result);
     result = sumloop(5, 6, 7);
    printf("%d", result);
  }
}
