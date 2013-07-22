#include <stdlib.h>
#include <stdio.h>

// Store on line 9 is trivially dead
int main(int argc, char** argv) {
  int *p = (int*)malloc(sizeof(int));

  if (argc) {
    *p = 0;
  }

  *p = 1;

  int x = *p;
}
