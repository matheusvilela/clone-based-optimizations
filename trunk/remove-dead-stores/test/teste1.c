#include <stdlib.h>
#include <stdio.h>

//Trivially dead store
int main(int argc, char** argv) {
  int *p = (int*)malloc(sizeof(int));
  *p = 0;
  *p = 1;
  int x = *p;
}
