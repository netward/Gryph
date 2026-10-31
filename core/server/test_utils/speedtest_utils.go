package test_utils

import (
	"GryphCore/internal"
	"GryphCore/internal/boxbox"
	"context"
	"errors"
	"fmt"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing/service"
	"io"
	"net"
	"net/http"
	"sync"
	"sync/atomic"
	"time"
)

var SpTQuerier SpeedTestResultQuerier
var CountryResults CountryTestResults

type SpeedTestResult struct {
	Tag           string
	DlSpeed       string
	UlSpeed       string
	Latency       int32
	ServerName    string
	ServerCountry string
	Error         error
	Cancelled     bool
}

type SpeedTestResultQuerier struct {
	active  int
	current SpeedTestResult
	mu      sync.RWMutex
}

func (s *SpeedTestResultQuerier) Result() (SpeedTestResult, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	return s.current, s.active > 0
}

func (s *SpeedTestResultQuerier) storeResult(result *SpeedTestResult) {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.current = *result
}

func (s *SpeedTestResultQuerier) beginTest() {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.active++
}

func (s *SpeedTestResultQuerier) endTest() {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.active <= 0 {
		panic("SpeedTestResultQuerier: unbalanced endTest")
	}

	s.active--
}

type CountryTestResults struct {
	results []*SpeedTestResult
	mu      sync.Mutex
}

func (c *CountryTestResults) AddResult(result *SpeedTestResult) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.results = append(c.results, result)
}

func (c *CountryTestResults) Results() []*SpeedTestResult {
	c.mu.Lock()
	defer c.mu.Unlock()
	cp := c.results
	c.results = nil
	return cp
}

func countryTest(ctx context.Context, dialer func(ctx context.Context, network string, address string) (net.Conn, error), res *SpeedTestResult) error {
	srv, err := getSpeedtestServer(ctx, dialer)
	if err != nil {
		return err
	}
	res.ServerName = srv.Name
	res.ServerCountry = srv.Country
	res.Latency = int32(srv.Latency.Milliseconds())
	return nil
}

func BatchSpeedTest(ctx context.Context, i *boxbox.Box, outboundTags []string, testDl, testUl bool, simpleDL bool, simpleAddress string, timeout time.Duration, countryOnly bool, countryConcurrency int32) []*SpeedTestResult {
	outbounds := service.FromContext[adapter.OutboundManager](i.Context())
	results := make([]*SpeedTestResult, 0)
	var queuer chan struct{}
	wg := &sync.WaitGroup{}
	if countryOnly {
		if countryConcurrency <= 0 {
			countryConcurrency = 5
		}
		queuer = make(chan struct{}, countryConcurrency)
	}

	for _, tag := range outboundTags {
		select {
		case <-ctx.Done():
			break
		default:
		}
		outbound, exists := outbounds.Outbound(tag)
		if !exists {
			panic("no outbound with tag " + tag + " found")
		}
		res := new(SpeedTestResult)
		res.Tag = tag
		results = append(results, res)

		var err error
		if countryOnly {
			queuer <- struct{}{}
			wg.Add(1)
			go func(res *SpeedTestResult, outbound adapter.Outbound) {
				defer func() { <-queuer }()
				defer wg.Done()
				err := countryTest(ctx, getNetDialer(outbound.DialContext), res)
				if err != nil && !errors.Is(err, context.Canceled) {
					res.Error = err
					fmt.Println("Failed to countryTest with err:", err)
				}
				CountryResults.AddResult(res)
			}(res, outbound)
			continue
		}
		if simpleDL {
			err = simpleDownloadTest(ctx, getNetDialer(outbound.DialContext), res, simpleAddress, timeout)
		} else {
			err = speedTestWithDialer(ctx, getNetDialer(outbound.DialContext), res, testDl, testUl, timeout)
		}
		if err != nil && !errors.Is(err, context.Canceled) {
			res.Error = err
			fmt.Println("Failed to speedtest with err:", err)
		}
		if !testDl && !simpleDL {
			res.DlSpeed = ""
		}
		if !testUl {
			res.UlSpeed = ""
		}
	}
	wg.Wait()

	return results
}

type atomicByteCounter struct {
	total atomic.Int64
}

func (c *atomicByteCounter) Write(p []byte) (int, error) {
	c.total.Add(int64(len(p)))
	return len(p), nil
}

func simpleDownloadTest(
	ctx context.Context,
	dialer func(context.Context, string, string) (net.Conn, error),
	res *SpeedTestResult,
	testURL string,
	timeout time.Duration,
) error {
	if timeout <= 0 {
		timeout = URLTestTimeout
	}

	transport := &http.Transport{
		DialContext: dialer,
	}
	defer transport.CloseIdleConnections()

	client := &http.Client{
		Transport: transport,
		Timeout:   timeout,
	}

	res.ServerName = "N/A"
	res.ServerCountry = "N/A"

	req, err := http.NewRequestWithContext(
		ctx, http.MethodGet, testURL, nil,
	)
	if err != nil {
		return err
	}

	var counter atomicByteCounter
	var latencyMs atomic.Int64
	var downloadStarted atomic.Pointer[time.Time]

	done := make(chan error, 1)

	SpTQuerier.beginTest()
	defer SpTQuerier.endTest()

	requestStarted := time.Now()

	go func() {
		resp, err := client.Do(req)
		if err != nil {
			done <- err
			return
		}
		defer resp.Body.Close()

		latencyMs.Store(
			time.Since(requestStarted).Milliseconds(),
		)

		started := time.Now()
		downloadStarted.Store(&started)

		_, err = io.Copy(&counter, resp.Body)
		done <- err
	}()

	publish := func() {
		res.Latency = int32(latencyMs.Load())

		if started := downloadStarted.Load(); started != nil {
			elapsed := time.Since(*started).Seconds()

			if elapsed > 0 {
				bytesPerSecond :=
					float64(counter.total.Load()) / elapsed

				res.DlSpeed = internal.BrateToStr(
					bytesPerSecond,
				)
			}
		}

		SpTQuerier.storeResult(res)
	}

	finish := func(workerErr error) error {
		if ctxErr := ctx.Err(); ctxErr != nil {
			res.Cancelled = true
			workerErr = ctxErr
		}

		res.Error = workerErr
		publish()

		return workerErr
	}

	ticker := time.NewTicker(50 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case workerErr := <-done:
			return finish(workerErr)

		case <-ctx.Done():
			// Wait until the HTTP worker has stopped.
			// The caller must not release its core reference
			// while the worker is still using the dialer.
			workerErr := <-done
			return finish(workerErr)

		case <-ticker.C:
			publish()
		}
	}
}

func speedTestWithDialer(ctx context.Context, dialer func(ctx context.Context, network string, address string) (net.Conn, error), res *SpeedTestResult, testDl, testUl bool, timeout time.Duration) error {
	srv, err := getSpeedtestServer(ctx, dialer)
	if err != nil {
		return err
	}
	res.ServerName = srv.Name
	res.ServerCountry = srv.Country

	done := make(chan struct{})

	SpTQuerier.beginTest()
	defer SpTQuerier.endTest()

	go func() {
		defer func() { close(done) }()
		if testDl {
			timeoutCtx, cancel := context.WithTimeout(ctx, timeout)
			defer cancel()
			err = srv.DownloadTestContext(timeoutCtx)
			if err != nil && !errors.Is(err, context.Canceled) {
				res.Error = err
				return
			}
		}
		if testUl {
			timeoutCtx, cancel := context.WithTimeout(ctx, timeout)
			defer cancel()
			err = srv.UploadTestContext(timeoutCtx)
			if err != nil && !errors.Is(err, context.Canceled) {
				res.Error = err
				return
			}
		}
	}()

	ticker := time.NewTicker(time.Millisecond * 50)
	defer ticker.Stop()

	for {
		select {
		case <-done:
			res.DlSpeed = internal.BrateToStr(float64(srv.DLSpeed))
			res.UlSpeed = internal.BrateToStr(float64(srv.ULSpeed))
			res.Latency = int32(srv.Latency.Milliseconds())
			SpTQuerier.storeResult(res)
			return nil
		case <-ctx.Done():
			res.Cancelled = true
			return ctx.Err()
		case <-ticker.C:
			res.DlSpeed = internal.BrateToStr(srv.Context.GetEWMADownloadRate())
			res.UlSpeed = internal.BrateToStr(srv.Context.GetEWMAUploadRate())
			res.Latency = int32(srv.Latency.Milliseconds())
			SpTQuerier.storeResult(res)
		}
	}
}
