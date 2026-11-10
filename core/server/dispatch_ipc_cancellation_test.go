package main

import (
	"context"
	"errors"
	"io"
	"net"
	"testing"
	"time"
)

func TestIPCSessionCancellationOnDisconnect(
	t *testing.T,
) {
	client, server := net.Pipe()
	defer client.Close()

	started := make(chan struct{})
	cancelled := make(chan struct{})
	done := make(chan error, 1)

	go func() {
		done <- serveIPCWithContext(
			server,
			func(
				ctx context.Context,
				method string,
				_ []byte,
			) ([]byte, error) {
				if method != "Test" {
					return nil, errors.New(
						"unexpected RPC method",
					)
				}

				close(started)

				// Simulate a long-running operation
				// that respects cancellation.
				<-ctx.Done()

				close(cancelled)

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
	case <-time.After(5 * time.Second):
		t.Fatal("RPC handler did not start")
	}

	// Simulate GUI disconnection.
	if err := client.Close(); err != nil {
		t.Fatal(err)
	}

	select {
	case err := <-done:
		if !errors.Is(err, io.EOF) {
			t.Fatalf(
				"unexpected IPC exit: %v",
				err,
			)
		}

	case <-time.After(5 * time.Second):
		t.Fatal("IPC reader did not exit")
	}

	// The running handler must observe
	// cancellation of the IPC session.
	select {
	case <-cancelled:

	case <-time.After(5 * time.Second):
		t.Fatal(
			"RPC handler was not cancelled",
		)
	}
}

func TestRPCContextBinding(
	t *testing.T,
) {
	rpcCtx, cancelRPC :=
		context.WithCancel(context.Background())

	operationCtx, release := bindRPCContext(
		context.Background(),
		rpcCtx,
	)
	defer release()

	cancelRPC()

	select {
	case <-operationCtx.Done():

	case <-time.After(5 * time.Second):
		t.Fatal(
			"operation context was not cancelled",
		)
	}
}
