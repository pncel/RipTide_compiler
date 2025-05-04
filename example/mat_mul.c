void matrixMultiply(const float *A, const float *B, float *C, int M, int K, int N) {
    // Loop over the rows of matrix A (M rows)
    for (int i = 0; i < M; ++i) {
        // Loop over the columns of matrix B (N columns)
        for (int j = 0; j < N; ++j) {
            // Calculate the dot product of the i-th row of A and the j-th column of B
            float sum = 0.0f;
            for (int k = 0; k < K; ++k) {
                // Perform the multiplication and accumulate the result
                sum += A[i * K + k] * B[k * N + j];
            }
            // Store the result in the corresponding element of matrix C
            C[i * N + j] = sum;
        }
    }
}
