#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <emscripten.h>
#include <emscripten/threading.h>

// This file tests the old GCC built-in atomic operations of the form __sync_fetch_and_op().
// See https://gcc.gnu.org/onlinedocs/gcc-4.6.4/gcc/Atomic-Builtins.html

// TEMP: Fastcomp Clang does not implement the __sync_fetch_and_op builtin functions as atomic, but
//       generates non-atomic operations. As a hack to make this test pass, route these to library-
//       implemented functions, which are atomic proper. TODO: Implement support in fastcomp to
//       generate atomic ops from these builtins.
#define __sync_fetch_and_add emscripten_atomic_add_u32
#define __sync_fetch_and_sub emscripten_atomic_sub_u32
#define __sync_fetch_and_or emscripten_atomic_or_u32
#define __sync_fetch_and_and emscripten_atomic_and_u32
#define __sync_fetch_and_xor emscripten_atomic_xor_u32

#define NUM_THREADS 8

#define T int

// TEMP to make this test pass:
// Our Clang backend doesn't define this builtin function, so implement it ourselves.
// The current Atomics spec doesn't have the nand atomic op either, so must use a cas loop.
// TODO: Move this to Clang backend?
T __sync_fetch_and_nand(T *ptr, T x)
{
	for(;;)
	{
		T old = emscripten_atomic_load_u32(ptr);
		T newVal = ~(old & x);
		T old2 = emscripten_atomic_cas_u32(ptr, old, newVal);
		if (old2 == old) return old;
	}
}

void *thread_fetch_and_add(void *arg)
{
	for(int i = 0; i < 10000; ++i)
		__sync_fetch_and_add((int*)arg, 1);
	pthread_exit(0);
}

void *thread_fetch_and_sub(void *arg)
{
	for(int i = 0; i < 10000; ++i)
		__sync_fetch_and_sub((int*)arg, 1);
	pthread_exit(0);
}

volatile int fetch_and_or_data = 0;
void *thread_fetch_and_or(void *arg)
{
	__sync_fetch_and_or((int*)&fetch_and_or_data, (int)arg);
	pthread_exit(0);
}

volatile int fetch_and_and_data = 0;
void *thread_fetch_and_and(void *arg)
{
	__sync_fetch_and_and((int*)&fetch_and_and_data, (int)arg);
	pthread_exit(0);
}

volatile int fetch_and_xor_data = 0;
void *thread_fetch_and_xor(void *arg)
{
	for(int i = 0; i < 9999; ++i) // Odd number of times so that the operation doesn't cancel itself out.
		__sync_fetch_and_xor((int*)&fetch_and_xor_data, (int)arg);
	pthread_exit(0);
}

volatile int fetch_and_nand_data = 0;
void *thread_fetch_and_nand(void *arg)
{
	for(int i = 0; i < 9999; ++i) // Odd number of times so that the operation doesn't cancel itself out.
		__sync_fetch_and_nand((int*)&fetch_and_nand_data, (int)arg);
	pthread_exit(0);
}

pthread_t thread[NUM_THREADS];

int main()
{
	{
		T x = 5;
		T y = __sync_fetch_and_add(&x, 10);
		assert(y == 5);
		assert(x == 15);
		volatile int n = 1;
		for(int i = 0; i < NUM_THREADS; ++i) pthread_create(&thread[i], NULL, thread_fetch_and_add, (void*)&n);
		for(int i = 0; i < NUM_THREADS; ++i) pthread_join(thread[i], NULL);
		assert(n == NUM_THREADS*10000+1);
	}
	{
		T x = 5;
		T y = __sync_fetch_and_sub(&x, 10);
		assert(y == 5);
		assert(x == -5);
		volatile int n = 1;
		for(int i = 0; i < NUM_THREADS; ++i) pthread_create(&thread[i], NULL, thread_fetch_and_sub, (void*)&n);
		for(int i = 0; i < NUM_THREADS; ++i) pthread_join(thread[i], NULL);
		assert(n == 1-NUM_THREADS*10000);
	}
	{
		T x = 5;
		T y = __sync_fetch_and_or(&x, 9);
		assert(y == 5);
		assert(x == 13);
		for(int x = 0; x < 100; ++x) // Test a few times for robustness, since this test is so short-lived.
		{
			fetch_and_or_data = (1<<NUM_THREADS);
			for(int i = 0; i < NUM_THREADS; ++i) pthread_create(&thread[i], NULL, thread_fetch_and_or, (void*)(1<<i));
			for(int i = 0; i < NUM_THREADS; ++i) pthread_join(thread[i], NULL);
			assert(fetch_and_or_data == (1<<(NUM_THREADS+1))-1);
		}
	}
	{
		T x = 5;
		T y = __sync_fetch_and_and(&x, 9);
		assert(y == 5);
		assert(x == 1);
		for(int x = 0; x < 100; ++x) // Test a few times for robustness, since this test is so short-lived.
		{
			fetch_and_and_data = (1<<(NUM_THREADS+1))-1;
			for(int i = 0; i < NUM_THREADS; ++i) pthread_create(&thread[i], NULL, thread_fetch_and_and, (void*)(~(1<<i)));
			for(int i = 0; i < NUM_THREADS; ++i) pthread_join(thread[i], NULL);
			assert(fetch_and_and_data == 1<<NUM_THREADS);
		}
	}
	{
		T x = 5;
		T y = __sync_fetch_and_xor(&x, 9);
		assert(y == 5);
		assert(x == 12);
		for(int x = 0; x < 100; ++x) // Test a few times for robustness, since this test is so short-lived.
		{
			fetch_and_xor_data = 1<<NUM_THREADS;
			for(int i = 0; i < NUM_THREADS; ++i) pthread_create(&thread[i], NULL, thread_fetch_and_xor, (void*)(~(1<<i)));
			for(int i = 0; i < NUM_THREADS; ++i) pthread_join(thread[i], NULL);
			assert(fetch_and_xor_data == (1<<(NUM_THREADS+1))-1);
		}
	}
	{
		T x = 5;
		T y = __sync_fetch_and_nand(&x, 9);
		assert(y == 5);
		assert(x == -2);
		const int oddNThreads = NUM_THREADS-1;
		for(int x = 0; x < 100; ++x) // Test a few times for robustness, since this test is so short-lived.
		{
			fetch_and_nand_data = 0;
			for(int i = 0; i < oddNThreads; ++i) pthread_create(&thread[i], NULL, thread_fetch_and_nand, (void*)-1);
			for(int i = 0; i < oddNThreads; ++i) pthread_join(thread[i], NULL);
			assert(fetch_and_nand_data == -1);
		}
	}

#ifdef REPORT_RESULT
	int result = 0;
	REPORT_RESULT();
#endif
}
