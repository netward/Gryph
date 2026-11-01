package main

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"GryphCore/gen"
	"GryphCore/test_utils"
)

func newPR4BenchmarkServer(b *testing.B) *httptest.Server {
	b.Helper()

	srv := httptest.NewServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			w.WriteHeader(http.StatusNoContent)
		}),
	)

	b.Cleanup(srv.Close)
	return srv
}

// Measures creation, startup, cancellation and shutdown
// of one temporary sing-box instance.
func BenchmarkPR4TemporaryRuntimeLifecycle(b *testing.B) {
	b.ReportAllocs()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		rt, err := newTemporaryTestRuntime(
			lifecycleTestConfig,
			false,
			"",
		)
		if err != nil {
			b.Fatal(err)
		}

		if err := rt.Close(); err != nil {
			b.Fatal(err)
		}
	}
}

// Measures the complete temporary URL Test RPC,
// including creation and closing of sing-box.
func BenchmarkPR4TemporaryURLRPC(b *testing.B) {
	srv := newPR4BenchmarkServer(b)
	s := &server{}

	test_utils.ResetTestContext()
	b.Cleanup(test_utils.ResetTestContext)

	req := &gen.TestReq{
		Config:             To(lifecycleTestConfig),
		TestCurrent:        To(false),
		UseDefaultOutbound: To(true),
		Url:                To(srv.URL),
		MaxConcurrency:     To(int32(1)),
		TestTimeoutMs:      To(int32(3000)),
	}

	b.ReportAllocs()
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		resp, err := s.Test(
			context.Background(),
			req,
		)
		if err != nil {
			b.Fatal(err)
		}

		if resp == nil || len(resp.GetResults()) != 1 {
			b.Fatalf("unexpected response: %v", resp)
		}

		if got := resp.GetResults()[0].GetError(); got != "" {
			b.Fatalf("URL Test failed: %s", got)
		}

		// Exclude reporter cleanup from measurement.
		b.StopTimer()
		_ = test_utils.URLReporter.Results()
		b.StartTimer()
	}
}

// Measures URL Test through an already running main core.
func BenchmarkPR4CurrentURLRPC(b *testing.B) {
	srv := newPR4BenchmarkServer(b)
	s := &server{}

	test_utils.ResetTestContext()
	b.Cleanup(test_utils.ResetTestContext)

	startResp, err := s.Start(
		context.Background(),
		&gen.LoadConfigReq{
			CoreConfig: To(lifecycleTestConfig),
		},
	)
	if err != nil || startResp == nil ||
		startResp.GetError() != "" {
		b.Fatalf(
			"Start failed: response=%v err=%v",
			startResp, err,
		)
	}

	b.Cleanup(func() {
		resp, err := s.Stop(
			context.Background(),
			&gen.EmptyReq{},
		)
		if err != nil || resp == nil ||
			resp.GetError() != "" {
			b.Errorf(
				"Stop failed: response=%v err=%v",
				resp, err,
			)
		}
	})

	req := &gen.TestReq{
		TestCurrent:        To(true),
		UseDefaultOutbound: To(true),
		Url:                To(srv.URL),
		MaxConcurrency:     To(int32(1)),
		TestTimeoutMs:      To(int32(3000)),
	}

	b.ReportAllocs()
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		resp, err := s.Test(
			context.Background(),
			req,
		)
		if err != nil {
			b.Fatal(err)
		}

		if resp == nil || len(resp.GetResults()) != 1 {
			b.Fatalf("unexpected response: %v", resp)
		}

		if got := resp.GetResults()[0].GetError(); got != "" {
			b.Fatalf("URL Test failed: %s", got)
		}

		b.StopTimer()
		_ = test_utils.URLReporter.Results()
		b.StartTimer()
	}
}

// Measures BatchURLTest with an already created box.
// This helps isolate network-test overhead from startup.
func BenchmarkPR4URLBatchReuse(b *testing.B) {
	srv := newPR4BenchmarkServer(b)

	rt, err := newTemporaryTestRuntime(
		lifecycleTestConfig,
		false,
		"",
	)
	if err != nil {
		b.Fatal(err)
	}

	b.Cleanup(func() {
		if err := rt.Close(); err != nil {
			b.Errorf("Close failed: %v", err)
		}
	})

	b.ReportAllocs()
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		results := test_utils.BatchURLTest(
			context.Background(),
			rt.box,
			[]string{"direct"},
			srv.URL,
			1,
			false,
			3*time.Second,
		)

		if len(results) != 1 ||
			results[0] == nil ||
			results[0].Error != nil {
			b.Fatalf(
				"BatchURLTest failed: %v", results,
			)
		}

		b.StopTimer()
		_ = test_utils.URLReporter.Results()
		b.StartTimer()
	}
}
