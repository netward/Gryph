package main

import (
	"context"
	"errors"
	"testing"
	"time"
)

func TestCurrentCoreTrackerWaitsForRelease(t *testing.T) {
	var tracker currentCoreTestTracker

	firstCtx, firstCancel := context.WithCancel(
		context.Background(),
	)
	secondCtx, secondCancel := context.WithCancel(
		context.Background(),
	)

	releaseFirst := tracker.register(firstCancel)
	releaseSecond := tracker.register(secondCancel)

	waitCtx, cancel := context.WithTimeout(
		context.Background(),
		50*time.Millisecond,
	)
	defer cancel()

	// Cancellation does not imply that workers have finished.
	err := tracker.cancelAndWait(waitCtx)

	if !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("expected wait timeout, got %v", err)
	}

	if firstCtx.Err() != context.Canceled {
		t.Fatal("first test was not cancelled")
	}

	if secondCtx.Err() != context.Canceled {
		t.Fatal("second test was not cancelled")
	}

	// One completed worker is not enough.
	releaseFirst()

	shortCtx, shortCancel := context.WithTimeout(
		context.Background(),
		50*time.Millisecond,
	)
	defer shortCancel()

	err = tracker.cancelAndWait(shortCtx)
	if !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("returned before second release: %v", err)
	}

	releaseSecond()

	// Once all users release their references,
	// shutdown must complete immediately.
	if err := tracker.cancelAndWait(
		context.Background(),
	); err != nil {
		t.Fatal(err)
	}
}