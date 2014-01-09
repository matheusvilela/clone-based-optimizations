#include <stdio.h>
#include <stdlib.h>

int sum(int a, int b) {
  return a+b;
}
int main() {
    int result = sum(1, 2);
    printf("%d", result);
     result = sum(1, 2);
    printf("%d", result);
     result = sum(result, 6);
    printf("%d", result);
}
