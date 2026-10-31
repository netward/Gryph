package main

import (
	"context"
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"GryphCore/gen"
	"GryphCore/test_utils"
)

func TestStopTestWaitsForCurrentURLTest(t *testing.T) {
	s := &server{}
	test_utils.ResetTestContext()

	t.Cleanup(func() {
		test_utils.ResetTestContext()

		ctx, cancel := context.WithTimeout(
			context.Background(),
			5*time.Second,
		)
		defer cancel()

		_, _ = s.Stop(ctx, &gen.EmptyReq{})
	})

	startResp, err := s.Start(
		context.Background(),
		&gen.LoadConfigReq{
			CoreConfig: To(lifecycleTestConfig),
		},
	)
	if err != nil || startResp == nil ||
		startResp.GetError() != "" {
		t.Fatalf("Start failed: %v, %v", startResp, err)
	}

	requestStarted := make(chan struct{})
	handlerDone := make(chan struct{})

	httpServer := httptest.NewServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			defer close(handlerDone)

			// Do not send response headers.
			// Keep URL Test waiting for the HTTP response.
			close(requestStarted)
			<-r.Context().Done()
		}),
	)

	t.Cleanup(func() {
		httpServer.CloseClientConnections()
		httpServer.Close()
	})

	testDone := make(chan error, 1)

	go func() {
		resp, err := s.Test(
			context.Background(),
			&gen.TestReq{
				TestCurrent:        To(true),
				UseDefaultOutbound: To(true),
				Url:                To(httpServer.URL),
				MaxConcurrency:     To(int32(1)),
				TestTimeoutMs:      To(int32(30000)),
			},
		)

		if err != nil {
			testDone <- err
			return
		}

		if resp == nil || len(resp.GetResults()) != 1 {
			testDone <- fmt.Errorf(
				"unexpected response: %v", resp,
			)
			return
		}

		if resp.GetResults()[0].GetError() == "" {
			testDone <- fmt.Errorf(
				"URL test was not cancelled: %v", resp,
			)
			return
		}

		testDone <- nil
	}()

	select {
	case <-requestStarted:
	case <-time.After(5 * time.Second):
		t.Fatal("URL Test did not start")
	}

	// Ensure the RPC has not already completed.
	select {
	case err := <-testDone:
		t.Fatalf(
			"URL Test finished before StopTest: %v",
			err,
		)
	case <-time.After(50 * time.Millisecond):
	}

	stopCtx, stopCancel := context.WithTimeout(
		context.Background(),
		8*time.Second,
	)
	defer stopCancel()

	stopResp, err := s.StopTest(
		stopCtx,
		&gen.EmptyReq{},
	)
	if err != nil || stopResp == nil {
		t.Fatalf(
			"StopTest failed: response=%v err=%v",
			stopResp, err,
		)
	}

	// StopTest must wait until the current-core
	// operation releases its reference.
	currentCoreTests.mu.Lock()
	active := len(currentCoreTests.active)
	currentCoreTests.mu.Unlock()

	if active != 0 {
		t.Fatalf(
			"%d current-core tests remain active",
			active,
		)
	}

	select {
	case err := <-testDone:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("URL Test did not finish")
	}

	select {
	case <-handlerDone:
	case <-time.After(5 * time.Second):
		t.Fatal("HTTP handler did not terminate")
	}

	// StopTest must not shut down the main core.
	// A new current-core test should work immediately.
	quickServer := httptest.NewServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			w.WriteHeader(http.StatusNoContent)
		}),
	)
	defer quickServer.Close()

	nextResp, err := s.Test(
		context.Background(),
		&gen.TestReq{
			TestCurrent:        To(true),
			UseDefaultOutbound: To(true),
			Url:                To(quickServer.URL),
			MaxConcurrency:     To(int32(1)),
			TestTimeoutMs:      To(int32(3000)),
		},
	)
	if err != nil {
		t.Fatal(err)
	}

	if nextResp == nil || len(nextResp.GetResults()) != 1 {
		t.Fatalf("unexpected next response: %v", nextResp)
	}

	if got := nextResp.GetResults()[0].GetError(); got != "" {
		t.Fatalf("next URL Test failed: %s", got)
	}
}
