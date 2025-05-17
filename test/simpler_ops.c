int example1(int A, int n) {
    int foo = A + 8;
    int output = 0;

    if(foo > 42) {
      output = 9 + n;
      foo += output;
    } else {
      output = A + foo;
      foo = foo * foo ;
    }

    return output + foo + n;
}