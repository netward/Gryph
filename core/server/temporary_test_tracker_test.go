package main

import (
	"context"
	"testing"
	"time"

	"GryphCore/test_utils"
)

func TestTemporaryTrackerStopWaitsForRelease(t *testing.T) {
	test_utils.ResetTestContext()
	t.Cleanup(test_utils.ResetTestContext)

	var tracker temporaryTestTracker

	testCtx, release := tracker.begin()
	defer release()

	stopDone := make(chan error, 1)

	go func() {
		ctx, cancel := context.WithTimeout(
			context.Background(), 3*time.Second,
		)
		defer cancel()

		stopDone <- tracker.cancelAndWait(ctx)
	}()

	// Cancellation must reach the running operation.
	select {
	case <-testCtx.Done():
	case <-time.After(2 * time.Second):
		t.Fatal("test was not cancelled")
	}

	// Stop must not return before cleanup finishes.
	select {
	case err := <-stopDone:
		t.Fatalf(
			"Stop returned before release: %v",
			err,
		)
	case <-time.After(50 * time.Millisecond):
	}

	release()

	select {
	case err := <-stopDone:
		if err != nil {
			t.Fatalf("Stop failed: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("Stop did not finish after release")
	}

	// A new operation must use a fresh context.
	nextCtx, nextRelease := tracker.begin()
	defer nextRelease()

	if err := nextCtx.Err(); err != nil {
		t.Fatalf(
			"new test inherited cancellation: %v",
			err,
		)
	}
}
