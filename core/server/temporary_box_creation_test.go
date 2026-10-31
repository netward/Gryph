package main

import (
	"fmt"
	"sync"
	"testing"

	"GryphCore/internal/boxmain"
)

func TestConcurrentTemporaryBoxCreation(t *testing.T) {
	const workers = 4
	const iterations = 5

	var wg sync.WaitGroup

	start := make(chan struct{})
	failures := make(chan error, workers*iterations)

	for worker := 0; worker < workers; worker++ {
		wg.Add(1)

		go func(worker int) {
			defer wg.Done()
			<-start

			for i := 0; i < iterations; i++ {
				instance, cancel, err := boxmain.Create(
					[]byte(lifecycleTestConfig),
				)

				if err != nil {
					failures <- fmt.Errorf(
						"worker %d iteration %d: Create: %w",
						worker, i, err,
					)
					return
				}

				// Cancel the instance context before closing.
				cancel()

				if err := instance.Close(); err != nil {
					failures <- fmt.Errorf(
						"worker %d iteration %d: Close: %w",
						worker, i, err,
					)
					return
				}
			}
		}(worker)
	}

	close(start)
	wg.Wait()
	close(failures)

	for err := range failures {
		t.Error(err)
	}
}
