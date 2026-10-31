package main

import (
	"context"
	"sync"

	"GryphCore/test_utils"
)

type temporaryTestOperation struct {
	cancel context.CancelFunc
	done   chan struct{}
}

type temporaryTestTracker struct {
	mu     sync.Mutex
	nextID uint64
	active map[uint64]*temporaryTestOperation
}

var temporaryTests temporaryTestTracker

func (t *temporaryTestTracker) begin() (
	context.Context,
	func(),
) {
	t.mu.Lock()
	defer t.mu.Unlock()

	// Capture the active test generation and register
	// the operation atomically relative to StopTest().
	ctx, cancel := context.WithCancel(
		test_utils.TestContext(),
	)

	if t.active == nil {
		t.active = make(
			map[uint64]*temporaryTestOperation,
		)
	}

	id := t.nextID
	t.nextID++

	op := &temporaryTestOperation{
		cancel: cancel,
		done:   make(chan struct{}),
	}
	t.active[id] = op

	var once sync.Once

	release := func() {
		once.Do(func() {
			cancel()

			t.mu.Lock()
			delete(t.active, id)
			close(op.done)
			t.mu.Unlock()
		})
	}

	return ctx, release
}

func (t *temporaryTestTracker) cancelAndWait(
	ctx context.Context,
) error {
	t.mu.Lock()

	// Cancel old test generation, then create a new one.
	test_utils.ResetTestContext()

	// Snapshot only operations already registered.
	pending := make(
		[]*temporaryTestOperation,
		0,
		len(t.active),
	)

	for _, op := range t.active {
		op.cancel()
		pending = append(pending, op)
	}

	t.mu.Unlock()

	// Wait until each operation has finished cleanup.
	for _, op := range pending {
		select {
		case <-op.done:
		case <-ctx.Done():
			return ctx.Err()
		}
	}

	return nil
}
