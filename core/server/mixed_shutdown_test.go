package main

import (
	"context"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"GryphCore/gen"
	"GryphCore/test_utils"
)

// Runs four active HTTP operations through both core ownership modes,
// then races StopTest against Stop. No external network access is required.
func TestPR35ConcurrentMixedShutdownAndRestart(t *testing.T) {
	const workers = 4

	s := &server{}
	test_utils.ResetTestContext()

	started := make(chan string, workers)
	handlerDone := make(chan string, workers)

	httpServer := httptest.NewServer(http.HandlerFunc(
		func(w http.ResponseWriter, r *http.Request) {
			if strings.HasSuffix(r.URL.Path, "-download") {
				w.WriteHeader(http.StatusOK)
				_, _ = w.Write([]byte("initial data"))
				w.(http.Flusher).Flush()
			}

			started <- r.URL.Path

			<-r.Context().Done()

			handlerDone <- r.URL.Path
		},
	))

	t.Cleanup(func() {
		httpServer.CloseClientConnections()
		httpServer.Close()
	})

	t.Cleanup(func() {
		ctx, cancel := context.WithTimeout(
			context.Background(),
			12*time.Second,
		)
		defer cancel()

		_, _ = s.StopTest(ctx, &gen.EmptyReq{})
		_, _ = s.Stop(ctx, &gen.EmptyReq{})

		test_utils.ResetTestContext()
	})

	startResp, err := s.Start(
		context.Background(),
		&gen.LoadConfigReq{
			CoreConfig: To(lifecycleTestConfig),
		},
	)

	if err != nil ||
		startResp == nil ||
		startResp.GetError() != "" {
		t.Fatalf(
			"Start: response=%v err=%v",
			startResp, err,
		)
	}

	runURL := func(current bool, url string) error {
		req := &gen.TestReq{
			TestCurrent:        To(current),
			UseDefaultOutbound: To(true),
			Url:                To(url),
			MaxConcurrency:     To(int32(1)),
			TestTimeoutMs:      To(int32(30000)),
		}

		if !current {
			req.Config = To(lifecycleTestConfig)
		}

		resp, err := s.Test(context.Background(), req)
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
				"URL test not cancelled: %v", resp,
			)
		}

		return nil
	}

	runDownload := func(current bool, url string) error {
		req := &gen.SpeedTestRequest{
			TestCurrent:        To(current),
			UseDefaultOutbound: To(true),
			TestDownload:       To(true),
			SimpleDownload:     To(true),
			SimpleDownloadAddr: To(url),
			TimeoutMs:          To(int32(30000)),
		}

		if !current {
			req.Config = To(lifecycleTestConfig)
		}

		resp, err := s.SpeedTest(
			context.Background(),
			req,
		)
		if err != nil {
			return err
		}

		if resp == nil || len(resp.GetResults()) != 1 {
			return fmt.Errorf(
				"unexpected download response: %v",
				resp,
			)
		}

		if !resp.GetResults()[0].GetCancelled() {
			return fmt.Errorf(
				"download not cancelled: %v",
				resp,
			)
		}

		return nil
	}

	type result struct {
		name string
		err  error
	}

	requests := []struct {
		name string
		run  func() error
	}{
		{
			"/current-url",
			func() error {
				return runURL(
					true,
					httpServer.URL+"/current-url",
				)
			},
		},
		{
			"/temporary-url",
			func() error {
				return runURL(
					false,
					httpServer.URL+"/temporary-url",
				)
			},
		},
		{
			"/current-download",
			func() error {
				return runDownload(
					true,
					httpServer.URL+"/current-download",
				)
			},
		},
		{
			"/temporary-download",
			func() error {
				return runDownload(
					false,
					httpServer.URL+"/temporary-download",
				)
			},
		},
	}

	rpcDone := make(chan result, workers)

	for _, req := range requests {
		req := req

		go func() {
			rpcDone <- result{
				req.name,
				req.run(),
			}
		}()
	}

	seen := make(map[string]bool)

	for len(seen) < workers {
		select {
		case name := <-started:
			if seen[name] {
				t.Fatalf(
					"duplicate HTTP request before cancellation: %s",
					name,
				)
			}
			seen[name] = true

		case r := <-rpcDone:
			t.Fatalf(
				"%s completed before cancellation: %v",
				r.name, r.err,
			)

		case <-time.After(8 * time.Second):
			t.Fatalf(
				"only %d/%d HTTP handlers started",
				len(seen), workers,
			)
		}
	}

	stopGate := make(chan struct{})
	stopDone := make(chan result, 2)

	go func() {
		<-stopGate

		ctx, cancel := context.WithTimeout(
			context.Background(),
			10*time.Second,
		)
		defer cancel()

		resp, err := s.StopTest(
			ctx,
			&gen.EmptyReq{},
		)

		if err == nil && resp == nil {
			err = fmt.Errorf(
				"StopTest returned nil response",
			)
		}

		stopDone <- result{"StopTest", err}
	}()

	go func() {
		<-stopGate

		ctx, cancel := context.WithTimeout(
			context.Background(),
			10*time.Second,
		)
		defer cancel()

		resp, err := s.Stop(
			ctx,
			&gen.EmptyReq{},
		)

		if err == nil &&
			(resp == nil || resp.GetError() != "") {
			err = fmt.Errorf(
				"Stop response: %v", resp,
			)
		}

		stopDone <- result{"Stop", err}
	}()

	// Release both stopping operations simultaneously.
	close(stopGate)

	for i := 0; i < 2; i++ {
		select {
		case r := <-stopDone:
			if r.err != nil {
				t.Fatalf(
					"%s failed: %v",
					r.name, r.err,
				)
			}

		case <-time.After(12 * time.Second):
			t.Fatal(
				"StopTest/Stop did not complete",
			)
		}
	}

	// All four RPC calls must finish.
	for i := 0; i < workers; i++ {
		select {
		case r := <-rpcDone:
			if r.err != nil {
				t.Fatalf(
					"%s: %v",
					r.name, r.err,
				)
			}

		case <-time.After(5 * time.Second):
			t.Fatal("RPC did not finish")
		}
	}

	// All HTTP handlers must finish.
	for i := 0; i < workers; i++ {
		select {
		case <-handlerDone:

		case <-time.After(5 * time.Second):
			t.Fatal(
				"HTTP handler did not finish",
			)
		}
	}

	temporaryTests.mu.Lock()
	temporaryActive := len(temporaryTests.active)
	temporaryTests.mu.Unlock()

	currentCoreTests.mu.Lock()
	currentActive := len(currentCoreTests.active)
	currentCoreTests.mu.Unlock()

	if temporaryActive != 0 || currentActive != 0 {
		t.Fatalf(
			"active operations remain: temporary=%d current=%d",
			temporaryActive,
			currentActive,
		)
	}

	assertRuntimeStopped(t)

	// Restart the main core after concurrent shutdown.
	startResp, err = s.Start(
		context.Background(),
		&gen.LoadConfigReq{
			CoreConfig: To(lifecycleTestConfig),
		},
	)

	if err != nil ||
		startResp == nil ||
		startResp.GetError() != "" {
		t.Fatalf(
			"restart: response=%v err=%v",
			startResp, err,
		)
	}

	quickServer := httptest.NewServer(
		http.HandlerFunc(
			func(w http.ResponseWriter, _ *http.Request) {
				w.WriteHeader(http.StatusNoContent)
			},
		),
	)
	defer quickServer.Close()

	next, err := s.Test(
		context.Background(),
		&gen.TestReq{
			TestCurrent:        To(true),
			UseDefaultOutbound: To(true),
			Url:                To(quickServer.URL),
			MaxConcurrency:     To(int32(1)),
			TestTimeoutMs:      To(int32(3000)),
		},
	)

	if err != nil ||
		next == nil ||
		len(next.GetResults()) != 1 {
		t.Fatalf(
			"post-restart URL test: response=%v err=%v",
			next, err,
		)
	}

	if next.GetResults()[0].GetError() != "" {
		t.Fatalf(
			"post-restart URL test: %s",
			next.GetResults()[0].GetError(),
		)
	}
}
