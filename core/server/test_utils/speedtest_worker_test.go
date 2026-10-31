package test_utils

import (
	"context"
	"errors"
	"testing"
	"time"
)

func TestAwaitSpeedTestWorkerCompletion(t *testing.T) {
	done := make(chan error, 1)

	expected := errors.New("worker failed")
	done <- expected

	err, cancelled := awaitSpeedTestWorker(
		context.Background(),
		done,
		func() {},
	)

	if !errors.Is(err, expected) {
		t.Fatalf("expected worker error, got %v", err)
	}

	if cancelled {
		t.Fatal("completed worker marked as cancelled")
	}
}

func TestAwaitSpeedTestWorkerWaitsForShutdown(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	done := make(chan error, 1)

	type outcome struct {
		err       error
		cancelled bool
	}

	returned := make(chan outcome, 1)

	go func() {
		err, cancelled := awaitSpeedTestWorker(
			ctx,
			done,
			func() {},
		)

		returned <- outcome{
			err:       err,
			cancelled: cancelled,
		}
	}()

	cancel()

	// Cancellation alone must not finish the caller
	// while the worker is still active.
	select {
	case <-returned:
		t.Fatal("returned before worker shutdown")

	case <-time.After(50 * time.Millisecond):
	}

	// Now simulate completion of the worker.
	done <- context.Canceled

	select {
	case result := <-returned:
		if !errors.Is(result.err, context.Canceled) {
			t.Fatalf(
				"expected context cancellation, got %v",
				result.err,
			)
		}

		if !result.cancelled {
			t.Fatal("worker was not marked as cancelled")
		}

	case <-time.After(2 * time.Second):
		t.Fatal("did not return after worker shutdown")
	}
}
