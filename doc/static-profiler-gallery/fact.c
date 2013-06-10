#include<stdio.h>
#include<stdlib.h>

long fact(int n) {
  long factorial = 1;

  while (n > 1) {
    factorial *= n--;
  }

  return factorial;
}

int main() {
  printf("%ld\n", fact(5));
}

