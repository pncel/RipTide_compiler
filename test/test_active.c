void example(int*A, int n, int m) {
  A[0] = 69;
  A[m+2] = 1;

  int foo = n - 6;

  if(foo > A[1]) {
      A[1] = foo;
  }
}