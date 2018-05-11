#include <stdio.h>
#include <stdlib.h>
#include <cuda.h>

__global__ void Add(int *a, int *b, int* c)
{
    *c = *a + *b; 
}

int main()
{
    // Host numbers
    int hostA;
    int hostB;
    int hostC;
    
    // Device numbers
    int* devA;
    int* devB;
    int* devC;

    // Allocate memory for device numbers
    cudaError_t err = cudaMalloc((void**)&devA, sizeof(int));
    if (err != cudaSuccess)
    {   
        printf("Failed to alloc memory for A, err: %s\n", cudaGetErrorString(err));
    }   

    err = cudaMalloc((void**)&devB, sizeof(int));
    if (err != cudaSuccess)
    {   
        printf("Failed to alloc memory for B, err: %s\n", cudaGetErrorString(err));
    }   
    
    err = cudaMalloc((void**)&devC, sizeof(int));
    if (err != cudaSuccess)
    {   
        printf("Failed to alloc memory for C, err: %s\n", cudaGetErrorString(err));
    }   

    hostA = 10; 
    hostB = 100;

    // Copy host values to device
    cudaMemcpy(devA, &hostA, sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(devB, &hostB, sizeof(int), cudaMemcpyHostToDevice);


    // Add on device
    Add<<<1,1>>>(devA, devB, devC);

    // Copy the result back to host
    cudaMemcpy(&hostC, devC, sizeof(int), cudaMemcpyDeviceToHost);

    // Deallocate memory
    cudaFree(devA);
    cudaFree(devB);
    cudaFree(devC);

    printf("A: %d, B: %d, C = A + B: %d\n", hostA, hostB, hostC);

    return 0;
}
