package main

import (
	"context"
	"sync"

	"GryphCore/internal/boxbox"
	"GryphCore/test_utils"
)

// Tracks operations using the currently running sing-box instance.
// Registration is protected by coreLifecycleMu.
type currentCoreTestTracker struct {
	mu     sync.Mutex
	nextID uint64
	active map[uint64]context.CancelFunc
	idle   chan struct{}
}

var currentCoreTests currentCoreTestTracker

func (t *currentCoreTestTracker) register(
	cancel context.CancelFunc,
) func() {
	t.mu.Lock()

	if t.active == nil {
		t.active = make(map[uint64]context.CancelFunc)
	}

	if len(t.active) == 0 {
		t.idle = make(chan struct{})
	}

	id := t.nextID
	t.nextID++

	t.active[id] = cancel
	t.mu.Unlock()

	return func() {
		cancel()

		t.mu.Lock()
		defer t.mu.Unlock()

		delete(t.active, id)

		if len(t.active) == 0 {
			close(t.idle)
			t.idle = nil
		}
	}
}

// Returns a borrowed instance, a cancellable test context,
// and a release function.
//
// Every successful acquisition MUST call release exactly once.
func acquireCurrentCoreTest() (
	*boxbox.Box,
	context.Context,
	func(),
) {
	coreLifecycleMu.RLock()
	defer coreLifecycleMu.RUnlock()

	if boxInstance == nil {
		return nil, nil, nil
	}

	ctx, cancel := context.WithCancel(
		test_utils.TestContext(),
	)

	release := currentCoreTests.register(cancel)

	return boxInstance, ctx, release
}

// The caller must hold coreLifecycleMu.Lock(), preventing
// registration of new users while shutdown is in progress.
func (t *currentCoreTestTracker) cancelAndWait(
	ctx context.Context,
) error {
	t.mu.Lock()

	if len(t.active) == 0 {
		t.mu.Unlock()
		return nil
	}

	for _, cancel := range t.active {
		cancel()
	}

	idle := t.idle
	t.mu.Unlock()

	select {
	case <-idle:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}