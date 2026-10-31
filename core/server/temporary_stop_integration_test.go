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

func runStopTestDuringTemporaryHTTP(
	t *testing.T,
	flushBeforeWait bool,
	run func(*server, string) error,
) {
	t.Helper()

	s := &server{}
	test_utils.ResetTestContext()

	requestStarted := make(chan struct{})
	handlerDone := make(chan struct{})

	httpServer := httptest.NewServer(
		http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			defer close(handlerDone)

			if flushBeforeWait {
				w.WriteHeader(http.StatusOK)
				_, _ = w.Write([]byte("initial data"))
				w.(http.Flusher).Flush()
			}

			close(requestStarted)

			// Wait until the client cancels the request.
			<-r.Context().Done()
		}),
	)

	t.Cleanup(func() {
		test_utils.ResetTestContext()
		httpServer.CloseClientConnections()
		httpServer.Close()
	})

	testDone := make(chan error, 1)

	go func() {
		testDone <- run(s, httpServer.URL)
	}()

	// Wait until the request reaches our HTTP server.
	select {
	case <-requestStarted:
	case <-time.After(5 * time.Second):
		t.Fatal("temporary HTTP test did not start")
	}

	// Verify that the operation is still running.
	select {
	case err := <-testDone:
		t.Fatalf(
			"temporary test finished before StopTest: %v",
			err,
		)
	case <-time.After(50 * time.Millisecond):
	}

	stopCtx, cancel := context.WithTimeout(
		context.Background(),
		8*time.Second,
	)
	defer cancel()

	stopResp, stopErr := s.StopTest(
		stopCtx,
		&gen.EmptyReq{},
	)

	if stopErr != nil {
		t.Fatalf("StopTest failed: %v", stopErr)
	}
	if stopResp == nil {
		t.Fatal("StopTest returned nil response")
	}

	// StopTest must cause the RPC to finish.
	select {
	case err := <-testDone:
		if err != nil {
			t.Fatalf("temporary test RPC: %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("temporary test did not finish")
	}

	// The server handler must observe cancellation.
	select {
	case <-handlerDone:
	case <-time.After(5 * time.Second):
		t.Fatal("HTTP handler did not terminate")
	}

	// The temporary runtime must be fully released.
	temporaryTests.mu.Lock()
	active := len(temporaryTests.active)
	temporaryTests.mu.Unlock()

	if active != 0 {
		t.Fatalf(
			"%d temporary operations remain active",
			active,
		)
	}

	// A new test must work after StopTest().
	quickServer := httptest.NewServer(
		http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			w.WriteHeader(http.StatusNoContent)
		}),
	)
	defer quickServer.Close()

	nextResp, err := s.Test(
		context.Background(),
		&gen.TestReq{
			Config:             To(lifecycleTestConfig),
			TestCurrent:        To(false),
			UseDefaultOutbound: To(true),
			Url:                To(quickServer.URL),
			MaxConcurrency:     To(int32(1)),
			TestTimeoutMs:      To(int32(3000)),
		},
	)

	if err != nil {
		t.Fatalf("new test failed: %v", err)
	}

	if nextResp == nil || len(nextResp.GetResults()) != 1 {
		t.Fatalf(
			"unexpected new test response: %v",
			nextResp,
		)
	}

	if nextResp.GetResults()[0].GetError() != "" {
		t.Fatalf(
			"new test inherited cancellation: %s",
			nextResp.GetResults()[0].GetError(),
		)
	}
}

func TestStopTestDuringTemporaryURLTest(t *testing.T) {
	runStopTestDuringTemporaryHTTP(
		t,
		false,
		func(s *server, url string) error {
			resp, err := s.Test(
				context.Background(),
				&gen.TestReq{
					Config:             To(lifecycleTestConfig),
					TestCurrent:        To(false),
					UseDefaultOutbound: To(true),
					Url:                To(url),
					MaxConcurrency:     To(int32(1)),
					TestTimeoutMs:      To(int32(30000)),
				},
			)

			if err != nil {
				return err
			}

			if resp == nil || len(resp.GetResults()) != 1 {
				return fmt.Errorf(
					"unexpected URL response: %v", resp,
				)
			}

			if resp.GetResults()[0].GetError() == "" {
				return fmt.Errorf(
					"URL test was not cancelled: %v", resp,
				)
			}

			return nil
		},
	)
}

func TestStopTestDuringTemporarySimpleDownload(t *testing.T) {
	runStopTestDuringTemporaryHTTP(
		t,
		true,
		func(s *server, url string) error {
			resp, err := s.SpeedTest(
				context.Background(),
				&gen.SpeedTestRequest{
					Config:             To(lifecycleTestConfig),
					TestCurrent:        To(false),
					UseDefaultOutbound: To(true),
					TestDownload:       To(true),
					SimpleDownload:     To(true),
					SimpleDownloadAddr: To(url),
					TimeoutMs:          To(int32(30000)),
				},
			)

			if err != nil {
				return err
			}

			if resp == nil || len(resp.GetResults()) != 1 {
				return fmt.Errorf(
					"unexpected SpeedTest response: %v",
					resp,
				)
			}

			if !resp.GetResults()[0].GetCancelled() {
				return fmt.Errorf(
					"download was not cancelled: %v",
					resp,
				)
			}

			return nil
		},
	)
}
