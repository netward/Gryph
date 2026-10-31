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

func TestConcurrentStopTestStopAndRestart(t *testing.T) {
	const workers = 4

	s := &server{}
	test_utils.ResetTestContext()

	t.Cleanup(func() {
		_, _ = s.Stop(context.Background(), &gen.EmptyReq{})
		test_utils.ResetTestContext()
	})

	startCore := func() error {
		resp, err := s.Start(
			context.Background(),
			&gen.LoadConfigReq{
				CoreConfig: To(lifecycleTestConfig),
			},
		)
		if err != nil {
			return err
		}
		if resp == nil || resp.GetError() != "" {
			return fmt.Errorf("Start: %v", resp)
		}
		return nil
	}

	stopCore := func() error {
		ctx, cancel := context.WithTimeout(
			context.Background(), 8*time.Second,
		)
		defer cancel()

		resp, err := s.Stop(ctx, &gen.EmptyReq{})
		if err != nil {
			return err
		}
		if resp == nil || resp.GetError() != "" {
			return fmt.Errorf("Stop: %v", resp)
		}
		return nil
	}

	if err := startCore(); err != nil {
		t.Fatal(err)
	}

	started := make(chan struct{}, workers)
	handlerDone := make(chan struct{}, workers)

	httpServer := httptest.NewServer(
		http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte("initial data"))
			w.(http.Flusher).Flush()

			started <- struct{}{}
			<-r.Context().Done()
			handlerDone <- struct{}{}
		}),
	)
	defer httpServer.Close()

	results := make(chan error, workers)

	for j := 0; j < workers; j++ {
		go func() {
			resp, err := s.SpeedTest(
				context.Background(),
				&gen.SpeedTestRequest{
					TestCurrent:        To(true),
					UseDefaultOutbound: To(true),
					TestDownload:       To(true),
					SimpleDownload:     To(true),
					SimpleDownloadAddr: To(httpServer.URL),
					TimeoutMs:         To(int32(30000)),
				},
			)

			if err != nil {
				results <- err
				return
			}
			if resp == nil || len(resp.GetResults()) != 1 {
				results <- fmt.Errorf(
					"unexpected response: %v", resp,
				)
				return
			}
			if !resp.GetResults()[0].GetCancelled() {
				results <- fmt.Errorf(
					"download was not cancelled: %v", resp,
				)
				return
			}
			results <- nil
		}()
	}

	for j := 0; j < workers; j++ {
		select {
		case <-started:
		case <-time.After(5 * time.Second):
			t.Fatal("not all downloads started")
		}
	}

	stopTestDone := make(chan error, 1)
	stopDone := make(chan error, 1)

	// Both operations begin while downloads are active.
	go func() {
		_, err := s.StopTest(
			context.Background(), &gen.EmptyReq{},
		)
		stopTestDone <- err
	}()

	go func() {
		stopDone <- stopCore()
	}()

	select {
	case err := <-stopTestDone:
		if err != nil {
			t.Fatalf("StopTest: %v", err)
		}
	case <-time.After(8 * time.Second):
		t.Fatal("StopTest timed out")
	}

	select {
	case err := <-stopDone:
		if err != nil {
			t.Fatalf("Stop: %v", err)
		}
	case <-time.After(12 * time.Second):
		t.Fatal("Stop timed out")
	}

	for j := 0; j < workers; j++ {
		select {
		case err := <-results:
			if err != nil {
				t.Fatal(err)
			}
		case <-time.After(5 * time.Second):
			t.Fatal("download RPC did not finish")
		}
	}

	for j := 0; j < workers; j++ {
		select {
		case <-handlerDone:
		case <-time.After(5 * time.Second):
			t.Fatal("HTTP handler did not finish")
		}
	}

	assertRuntimeStopped(t)

	// Verify that shutdown left the lifecycle reusable.
	if err := startCore(); err != nil {
		t.Fatalf("restart failed: %v", err)
	}

	if err := stopCore(); err != nil {
		t.Fatalf("stop after restart failed: %v", err)
	}

	assertRuntimeStopped(t)
}