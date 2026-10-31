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

func runStopWithActiveHTTPTest(
	t *testing.T,
	run func(*server, string) error,
) {
	t.Helper()

	s := &server{}
	test_utils.ResetTestContext()

	t.Cleanup(func() {
		test_utils.ResetTestContext()

		ctx, cancel := context.WithTimeout(
			context.Background(), 3*time.Second,
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
	if err != nil {
		t.Fatal(err)
	}
	if startResp == nil || startResp.GetError() != "" {
		t.Fatalf("Start failed: %v", startResp)
	}

	requestStarted := make(chan struct{})
	handlerDone := make(chan struct{})

	httpServer := httptest.NewServer(
		http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			defer close(handlerDone)

			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte("initial data"))
			w.(http.Flusher).Flush()

			close(requestStarted)

			// Keep the response active until the client
			// cancels its request.
			<-r.Context().Done()
		}),
	)

	t.Cleanup(func() {
		httpServer.CloseClientConnections()
		httpServer.Close()
	})

	testDone := make(chan error, 1)

	go func() {
		testDone <- run(s, httpServer.URL)
	}()

	select {
	case <-requestStarted:
		// The HTTP test is actively using the core.
	case <-time.After(5 * time.Second):
		t.Fatal("HTTP test did not start")
	}

	stopCtx, cancelStop := context.WithTimeout(
		context.Background(), 8*time.Second,
	)
	defer cancelStop()

	stopResp, stopErr := s.Stop(
		stopCtx,
		&gen.EmptyReq{},
	)

	if stopErr != nil {
		t.Fatalf("Stop RPC failed: %v", stopErr)
	}
	if stopResp == nil || stopResp.GetError() != "" {
		t.Fatalf("Stop failed: %v", stopResp)
	}

	select {
	case err := <-testDone:
		if err != nil {
			t.Fatalf("test RPC failed: %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("test RPC did not finish after Stop")
	}

	select {
	case <-handlerDone:
	case <-time.After(5 * time.Second):
		t.Fatal("HTTP handler did not terminate")
	}

	assertRuntimeStopped(t)
}

func TestStopDuringCurrentURLTest(t *testing.T) {
	runStopWithActiveHTTPTest(
		t,
		func(s *server, url string) error {
			resp, err := s.Test(
				context.Background(),
				&gen.TestReq{
					TestCurrent:       To(true),
					UseDefaultOutbound: To(true),
					Url:               To(url),
					MaxConcurrency:    To(int32(1)),
					TestTimeoutMs:     To(int32(30000)),
				},
			)
			if err != nil {
				return err
			}
			if resp == nil || len(resp.GetResults()) != 1 {
				return fmt.Errorf(
					"unexpected URL test response: %v", resp,
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

func TestStopDuringCurrentSimpleDownload(t *testing.T) {
	runStopWithActiveHTTPTest(
		t,
		func(s *server, url string) error {
			resp, err := s.SpeedTest(
				context.Background(),
				&gen.SpeedTestRequest{
					TestCurrent:        To(true),
					UseDefaultOutbound: To(true),
					TestDownload:       To(true),
					TestUpload:         To(false),
					SimpleDownload:     To(true),
					SimpleDownloadAddr: To(url),
					OnlyCountry:        To(false),
					CountryConcurrency: To(int32(1)),
					TimeoutMs:         To(int32(30000)),
				},
			)
			if err != nil {
				return err
			}
			if resp == nil || len(resp.GetResults()) != 1 {
				return fmt.Errorf(
					"unexpected speed test response: %v", resp,
				)
			}
			if !resp.GetResults()[0].GetCancelled() {
				return fmt.Errorf(
					"speed test was not cancelled: %v", resp,
				)
			}
			return nil
		},
	)
}