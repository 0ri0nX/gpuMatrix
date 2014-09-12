all: test linearCombiner compute

test: test.cu matrix.h
	nvcc -o test -D CUDA test.cu -lcublas -lcurand -D DEBUG

linearCombiner: linearCombiner.cu matrix.h
	nvcc -o linearCombiner -D CUDA linearCombiner.cu -lcublas -lcurand

compute: compute.cu matrix.h
	nvcc -o compute -D CUDA compute.cu -lcublas -lcurand

clean:
	rm -f linearCombiner test compute
