package main

import (
	"bytes"
	"context"
	"fmt"
	"net/http"
	"net/http/httptest"
	"runtime"
	"runtime/pprof"
	"sync"
	"testing"
	"time"

	"GryphCore/gen"
	"GryphCore/test_utils"
)

func TestPR35TemporaryTestsNoGoroutineGrowth(t *testing.T) {
	const (
		warmupWaves   = 3
		batches       = 4
		wavesPerBatch = 6
	)

	test_utils.ResetTestContext()
	t.Cleanup(test_utils.ResetTestContext)

	httpServer := httptest.NewServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			switch r.URL.Path {
			case "/ip":
				w.Header().Set(
					"Content-Type",
					"application/json",
				)
				_, _ = w.Write([]byte(
					`{"ip":"198.51.100.27","country_code":"ZZ"}`,
				))

			case "/download":
				_, _ = w.Write([]byte(
					"small-download-payload",
				))

			default:
				w.WriteHeader(http.StatusNoContent)
			}
		}),
	)
	defer httpServer.Close()

	s := &server{
		ipTestEndpoint: httpServer.URL + "/ip",
	}

	runWave := func() error {
		operations := []func() error{
			func() error {
				resp, err := s.Test(
					context.Background(),
					&gen.TestReq{
						Config:             To(lifecycleTestConfig),
						TestCurrent:        To(false),
						UseDefaultOutbound: To(true),
						Url:                To(httpServer.URL + "/url"),
						MaxConcurrency:     To(int32(1)),
						TestTimeoutMs:      To(int32(3000)),
					},
				)

				if err != nil {
					return fmt.Errorf("URL Test: %w", err)
				}

				if resp == nil ||
					len(resp.GetResults()) != 1 ||
					resp.GetResults()[0].GetError() != "" {
					return fmt.Errorf(
						"URL Test result: %v", resp,
					)
				}

				return nil
			},

			func() error {
				resp, err := s.IPTest(
					context.Background(),
					&gen.IPTestRequest{
						Config:             To(lifecycleTestConfig),
						UseDefaultOutbound: To(true),
						MaxConcurrency:     To(int32(1)),
						TestTimeoutMs:      To(int32(3000)),
					},
				)

				if err != nil {
					return fmt.Errorf("IPTest: %w", err)
				}

				if resp == nil || len(resp.GetResults()) != 1 {
					return fmt.Errorf(
						"IPTest result: %v", resp,
					)
				}

				info := resp.GetResults()[0]

				if info.GetError() != "" ||
					info.GetIp() != "198.51.100.27" ||
					info.GetCountryCode() != "ZZ" {
					return fmt.Errorf(
						"IPTest result: %v", info,
					)
				}

				return nil
			},

			func() error {
				resp, err := s.SpeedTest(
					context.Background(),
					&gen.SpeedTestRequest{
						Config:             To(lifecycleTestConfig),
						TestCurrent:        To(false),
						UseDefaultOutbound: To(true),
						TestDownload:       To(true),
						SimpleDownload:     To(true),
						SimpleDownloadAddr: To(
							httpServer.URL + "/download",
						),
						TimeoutMs: To(int32(3000)),
					},
				)

				if err != nil {
					return fmt.Errorf("SpeedTest: %w", err)
				}

				if resp == nil || len(resp.GetResults()) != 1 {
					return fmt.Errorf(
						"SpeedTest result: %v", resp,
					)
				}

				res := resp.GetResults()[0]

				if res.GetCancelled() || res.GetError() != "" {
					return fmt.Errorf(
						"SpeedTest result: %v", res,
					)
				}

				return nil
			},
		}

		results := make(chan error, len(operations))
		var wg sync.WaitGroup

		for _, run := range operations {
			wg.Add(1)

			go func(run func() error) {
				defer wg.Done()
				results <- run()
			}(run)
		}

		wg.Wait()
		close(results)

		for err := range results {
			if err != nil {
				return err
			}
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
			return fmt.Errorf(
				"StopTest: response=%v err=%v",
				stopResp, err,
			)
		}

		temporaryTests.mu.Lock()
		active := len(temporaryTests.active)
		temporaryTests.mu.Unlock()

		if active != 0 {
			return fmt.Errorf(
				"temporary operations remain active: %d",
				active,
			)
		}

		if _, running := test_utils.SpTQuerier.Result(); running {
			return fmt.Errorf(
				"speed-test worker remains active",
			)
		}

		// Clear accumulated test reporter records.
		_ = test_utils.URLReporter.Results()

		return nil
	}

	// Warm up lazy runtime and library initialization.
	for i := 0; i < warmupWaves; i++ {
		if err := runWave(); err != nil {
			t.Fatalf("warmup wave %d: %v", i, err)
		}
	}

	sample := func() int {
		runtime.GC()
		runtime.Gosched()

		time.Sleep(200 * time.Millisecond)

		runtime.GC()
		return runtime.NumGoroutine()
	}

	baseline := sample()
	samples := make([]int, 0, batches)

	for batch := 0; batch < batches; batch++ {
		for i := 0; i < wavesPerBatch; i++ {
			if err := runWave(); err != nil {
				t.Fatalf(
					"batch %d wave %d: %v",
					batch+1,
					i+1,
					err,
				)
			}
		}

		samples = append(samples, sample())

		t.Logf(
			"goroutines after batch %d: %d (baseline %d)",
			batch+1,
			samples[batch],
			baseline,
		)
	}

	last := samples[len(samples)-1]

	// Detect substantial continuing growth, while
	// allowing ordinary runtime fluctuations.
	if last > baseline+16 && last > samples[0]+10 {
		var stacks bytes.Buffer

		if profile := pprof.Lookup("goroutine"); profile != nil {
			_ = profile.WriteTo(&stacks, 2)
		}

		t.Errorf(
			"possible accumulating goroutines: baseline=%d samples=%v",
			baseline,
			samples,
		)

		t.Logf(
			"goroutine stacks:\n%s",
			stacks.String(),
		)
	}
}
