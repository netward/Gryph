package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"GryphCore/gen"
	"GryphCore/test_utils"

	"google.golang.org/protobuf/proto"
)

func TestIPCDisconnectReleasesTemporaryIPTest(
	t *testing.T,
) {
	test_utils.ResetTestContext()
	t.Cleanup(test_utils.ResetTestContext)

	requestStarted := make(chan struct{})
	requestFinished := make(chan struct{})

	httpServer := httptest.NewServer(
		http.HandlerFunc(
			func(
				w http.ResponseWriter,
				r *http.Request,
			) {
				defer close(requestFinished)

				// Do not send response headers.
				// The IPTest must remain active.
				close(requestStarted)

				<-r.Context().Done()
			},
		),
	)

	client, serverConn := net.Pipe()

	t.Cleanup(func() {
		_ = client.Close()
		httpServer.CloseClientConnections()
		httpServer.Close()
	})

	// Use a dedicated server instance so that
	// the global production server is not modified.
	testService := &server{
		ipTestEndpoint: httpServer.URL,
	}

	handlerDone := make(chan error, 1)
	serveDone := make(chan error, 1)

	go func() {
		serveDone <- serveIPCWithContext(
			serverConn,
			func(
				ctx context.Context,
				method string,
				payload []byte,
			) ([]byte, error) {
				if method != "IPTest" {
					err := fmt.Errorf(
						"unexpected RPC method: %s",
						method,
					)
					handlerDone <- err
					return nil, err
				}

				var req gen.IPTestRequest

				if err := proto.Unmarshal(
					payload,
					&req,
				); err != nil {
					handlerDone <- err
					return nil, err
				}

				// Run the real IPTest implementation.
				resp, err := testService.IPTest(
					ctx,
					&req,
				)

				if err != nil {
					handlerDone <- err
					return nil, err
				}

				if resp == nil ||
					len(resp.GetResults()) != 1 {
					err = fmt.Errorf(
						"unexpected IPTest response: %v",
						resp,
					)

					handlerDone <- err
					return nil, err
				}

				// The request must have been
				// interrupted by cancellation.
				if resp.GetResults()[0].GetError() == "" {
					err = errors.New(
						"IPTest completed without cancellation",
					)

					handlerDone <- err
					return nil, err
				}

				data, err := proto.Marshal(resp)
				handlerDone <- err

				return data, err
			},
		)
	}()

	if err := client.SetWriteDeadline(
		time.Now().Add(5 * time.Second),
	); err != nil {
		t.Fatal(err)
	}

	payload, err := proto.Marshal(
		&gen.IPTestRequest{
			Config: To(lifecycleTestConfig),

			UseDefaultOutbound: To(true),
			MaxConcurrency:     To(int32(1)),
			TestTimeoutMs:      To(int32(30000)),
		},
	)

	if err != nil {
		t.Fatal(err)
	}

	// Send the real IPC request frame.
	if err := writeIPCPayloadRequest(
		client,
		1,
		"IPTest",
		payload,
	); err != nil {
		t.Fatalf(
			"send IPTest request: %v",
			err,
		)
	}

	// Verify that the actual network request
	// reached the local HTTP server.
	select {
	case <-requestStarted:

	case <-time.After(8 * time.Second):
		t.Fatal(
			"IPTest did not reach HTTP server",
		)
	}

	// Simulate unexpected GUI disconnection.
	if err := client.Close(); err != nil {
		t.Fatal(err)
	}

	// The IPC dispatcher must wait until
	// the real IPTest handler has completed.
	select {
	case err := <-serveDone:
		if !errors.Is(err, io.EOF) {
			t.Fatalf(
				"unexpected IPC exit: %v",
				err,
			)
		}

	case <-time.After(8 * time.Second):
		t.Fatal(
			"IPC did not exit after disconnect",
		)
	}

	// Confirm that the RPC itself finished.
	select {
	case err := <-handlerDone:
		if err != nil {
			t.Fatalf(
				"IPTest cleanup failed: %v",
				err,
			)
		}

	default:
		t.Fatal(
			"IPC returned before IPTest completed",
		)
	}

	// The HTTP request must be cancelled.
	select {
	case <-requestFinished:

	case <-time.After(3 * time.Second):
		t.Fatal(
			"IPTest HTTP request was not cancelled",
		)
	}

	// Verify complete release of the
	// temporary test registration.
	temporaryTests.mu.Lock()
	active := len(temporaryTests.active)
	temporaryTests.mu.Unlock()

	if active != 0 {
		t.Fatalf(
			"%d temporary test operations remain",
			active,
		)
	}
}
