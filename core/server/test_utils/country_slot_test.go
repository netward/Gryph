package test_utils

import (
	"context"
	"testing"
	"time"
)

func TestAcquireCountrySlotAlreadyCancelled(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	slots := make(chan struct{}, 1)

	if acquireCountrySlot(ctx, slots) {
		t.Fatal("acquired slot after cancellation")
	}

	if len(slots) != 0 {
		t.Fatal("cancelled operation leaked a slot")
	}
}

func TestAcquireCountrySlotCancelledWhileFull(t *testing.T) {
	slots := make(chan struct{}, 1)
	slots <- struct{}{}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	done := make(chan bool, 1)

	go func() {
		done <- acquireCountrySlot(ctx, slots)
	}()

	cancel()

	select {
	case acquired := <-done:
		if acquired {
			t.Fatal("acquired a full slot after cancellation")
		}

	case <-time.After(2 * time.Second):
		t.Fatal("slot acquisition did not stop")
	}

	if len(slots) != 1 {
		t.Fatal("slot count changed unexpectedly")
	}
}