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

func TestPR35StopTestDuringActiveIPTest(t *testing.T) {
	test_utils.ResetTestContext()

	requestStarted := make(chan struct{})
	handlerDone := make(chan struct{})

	httpServer := httptest.NewServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			defer close(handlerDone)

			// Keep the connection open without sending
			// response headers.
			close(requestStarted)
			<-r.Context().Done()
		}),
	)

	s := &server{
		ipTestEndpoint: httpServer.URL,
	}

	t.Cleanup(func() {
		ctx, cancel := context.WithTimeout(
			context.Background(),
			8*time.Second,
		)
		defer cancel()

		_, _ = s.StopTest(ctx, &gen.EmptyReq{})

		httpServer.CloseClientConnections()
		httpServer.Close()
		test_utils.ResetTestContext()
	})

	testDone := make(chan error, 1)

	go func() {
		resp, err := s.IPTest(
			context.Background(),
			&gen.IPTestRequest{
				Config:             To(lifecycleTestConfig),
				UseDefaultOutbound: To(true),
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
				"unexpected IPTest response: %v",
				resp,
			)
			return
		}

		if resp.GetResults()[0].GetError() == "" {
			testDone <- fmt.Errorf(
				"IPTest was not cancelled: %v",
				resp,
			)
			return
		}

		testDone <- nil
	}()

	select {
	case <-requestStarted:
	case <-time.After(5 * time.Second):
		t.Fatal("IPTest HTTP request did not start")
	}

	// The HTTP operation must still be active.
	select {
	case err := <-testDone:
		t.Fatalf(
			"IPTest completed before StopTest: %v",
			err,
		)
	case <-time.After(50 * time.Millisecond):
	}

	stopCtx, cancel := context.WithTimeout(
		context.Background(),
		8*time.Second,
	)
	defer cancel()

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

	// The RPC must finish after cancellation.
	select {
	case err := <-testDone:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("IPTest did not finish after StopTest")
	}

	// The HTTP server must observe client cancellation.
	select {
	case <-handlerDone:
	case <-time.After(5 * time.Second):
		t.Fatal("IPTest HTTP handler did not terminate")
	}

	// StopTest must wait until the temporary runtime
	// has released its registration.
	temporaryTests.mu.Lock()
	active := len(temporaryTests.active)
	temporaryTests.mu.Unlock()

	if active != 0 {
		t.Fatalf(
			"%d temporary operations remain",
			active,
		)
	}

	// Verify that the next IPTest succeeds using
	// a fresh test context and a local JSON response.
	quickServer := httptest.NewServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			w.Header().Set(
				"Content-Type",
				"application/json",
			)

			_, _ = w.Write([]byte(
				`{"ip":"198.51.100.27","country_code":"ZZ"}`,
			))
		}),
	)
	defer quickServer.Close()

	nextServer := &server{
		ipTestEndpoint: quickServer.URL,
	}

	nextResp, err := nextServer.IPTest(
		context.Background(),
		&gen.IPTestRequest{
			Config:             To(lifecycleTestConfig),
			UseDefaultOutbound: To(true),
			MaxConcurrency:     To(int32(1)),
			TestTimeoutMs:      To(int32(3000)),
		},
	)

	if err != nil {
		t.Fatalf("next IPTest failed: %v", err)
	}

	if nextResp == nil || len(nextResp.GetResults()) != 1 {
		t.Fatalf(
			"unexpected next response: %v",
			nextResp,
		)
	}

	result := nextResp.GetResults()[0]

	if result.GetError() != "" {
		t.Fatalf(
			"next IPTest returned error: %s",
			result.GetError(),
		)
	}

	if result.GetIp() != "198.51.100.27" ||
		result.GetCountryCode() != "ZZ" {
		t.Fatalf(
			"unexpected IP information: %v",
			result,
		)
	}
}
