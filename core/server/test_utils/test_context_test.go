package test_utils

import (
	"context"
	"sync"
	"testing"
)

func TestContextResetCancelsPrevious(t *testing.T) {
	ResetTestContext()

	first := TestContext()
	if first.Err() != nil {
		t.Fatal("initial context is already cancelled")
	}

	ResetTestContext()

	if first.Err() != context.Canceled {
		t.Fatal("old context was not cancelled")
	}

	second := TestContext()

	if second == first {
		t.Fatal("context was not replaced")
	}

	if second.Err() != nil {
		t.Fatal("new context is already cancelled")
	}
}

func TestContextConcurrentReset(t *testing.T) {
	const workers = 8
	const iterations = 100

	ResetTestContext()

	start := make(chan struct{})
	var wg sync.WaitGroup

	for i := 0; i < workers; i++ {
		wg.Add(1)

		go func(worker int) {
			defer wg.Done()
			<-start

			for j := 0; j < iterations; j++ {
				if worker%2 == 0 {
					ResetTestContext()
				} else {
					ctx := TestContext()
					_ = ctx.Done()
					_ = ctx.Err()
				}
			}
		}(i)
	}

	close(start)
	wg.Wait()

	if TestContext().Err() != nil {
		t.Fatal("final context is cancelled")
	}
}
