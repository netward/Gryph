package test_utils

import (
	"sync"
	"testing"
)

func TestSpeedTestQuerierOverlappingRuns(t *testing.T) {
	var q SpeedTestResultQuerier

	q.beginTest()
	q.beginTest()

	q.endTest()

	_, running := q.Result()
	if !running {
		t.Fatal("second speed test is still active")
	}

	q.endTest()

	_, running = q.Result()
	if running {
		t.Fatal("all speed tests have finished")
	}
}

func TestSpeedTestQuerierConcurrentAccess(t *testing.T) {
	var q SpeedTestResultQuerier

	const workers = 8
	const iterations = 100

	var wg sync.WaitGroup

	for i := 0; i < workers; i++ {
		wg.Add(1)

		go func() {
			defer wg.Done()

			for j := 0; j < iterations; j++ {
				q.beginTest()

				q.storeResult(&SpeedTestResult{
					Tag: "direct",
				})

				_, _ = q.Result()

				q.endTest()
			}
		}()
	}

	wg.Wait()

	_, running := q.Result()
	if running {
		t.Fatal("running counter was not cleared")
	}
}
