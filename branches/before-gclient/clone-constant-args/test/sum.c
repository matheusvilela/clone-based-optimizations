#include <stdio.h>
#include <stdlib.h>

int sum(int a, int b) {
  return a+b;
}
int main() {
  for (int i = 0; i < 10000000; i++) {
    int result = sum(1, 2);
    printf("%d", result);
     result = sum(1, 2);
    printf("%d", result);
     result = sum(5, 6);
    printf("%d", result);
  }
}
