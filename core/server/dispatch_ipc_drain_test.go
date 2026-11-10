package main

import (
	"context"
	"errors"
	"io"
	"net"
	"sync"
	"testing"
	"time"
)

func TestIPCWaitsForHandlerCleanup(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()

	started := make(chan struct{})
	cancelSeen := make(chan struct{})
	allowCleanup := make(chan struct{})
	handlerDone := make(chan struct{})
	serveDone := make(chan error, 1)

	var once sync.Once
	releaseCleanup := func() {
		once.Do(func() {
			close(allowCleanup)
		})
	}
	defer releaseCleanup()

	go func() {
		serveDone <- serveIPCWithContext(
			server,
			func(
				ctx context.Context,
				method string,
				_ []byte,
			) ([]byte, error) {
				defer close(handlerDone)

				if method != "Test" {
					return nil, errors.New(
						"unexpected method",
					)
				}

				close(started)

				// Wait for IPC disconnection.
				<-ctx.Done()
				close(cancelSeen)

				// Simulate resource cleanup that
				// continues after cancellation.
				<-allowCleanup

				return nil, ctx.Err()
			},
		)
	}()

	if err := client.SetDeadline(
		time.Now().Add(5 * time.Second),
	); err != nil {
		t.Fatal(err)
	}

	if err := writeIPCStressRequest(
		client,
		1,
		"Test",
	); err != nil {
		t.Fatal(err)
	}

	select {
	case <-started:
	case <-time.After(3 * time.Second):
		t.Fatal("RPC handler did not start")
	}

	// Simulate unexpected GUI disconnection.
	if err := client.Close(); err != nil {
		t.Fatal(err)
	}

	select {
	case <-cancelSeen:
	case <-time.After(3 * time.Second):
		t.Fatal("RPC did not observe cancellation")
	}

	// The handler is deliberately still cleaning up.
	// serveIPCWithContext must not return yet.
	select {
	case err := <-serveDone:
		t.Fatalf(
			"IPC returned before handler cleanup: %v",
			err,
		)

	case <-time.After(80 * time.Millisecond):
		// Expected: still waiting for cleanup.
	}

	// Permit the handler to finish.
	releaseCleanup()

	select {
	case err := <-serveDone:
		if !errors.Is(err, io.EOF) {
			t.Fatalf(
				"unexpected IPC exit: %v",
				err,
			)
		}

	case <-time.After(3 * time.Second):
		t.Fatal("IPC did not finish after cleanup")
	}

	select {
	case <-handlerDone:
	default:
		t.Fatal("handler was not finished")
	}
}
