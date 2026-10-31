package test_utils

import (
	"GryphCore/internal/boxbox"
	"context"
	"fmt"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing/common/metadata"
	"github.com/sagernet/sing/service"
	"net"
	"net/http"
	"sync"
	"time"
)

var URLReporter URLTestReporter

const URLTestTimeout = 3 * time.Second

type URLTestResult struct {
	Duration time.Duration
	Tag      string
	Error    error
}

type URLTestReporter struct {
	results []*URLTestResult
	mu      sync.Mutex
}

func (u *URLTestReporter) AddResult(result *URLTestResult) {
	u.mu.Lock()
	defer u.mu.Unlock()
	u.results = append(u.results, result)
}

func (u *URLTestReporter) Results() []*URLTestResult {
	u.mu.Lock()
	defer u.mu.Unlock()
	res := u.results
	u.results = nil
	return res
}

func BatchURLTest(
	ctx context.Context,
	i *boxbox.Box,
	outboundTags []string,
	url string,
	maxConcurrency int,
	twice bool,
	timeout time.Duration,
) []*URLTestResult {
	if timeout <= 0 {
		timeout = URLTestTimeout
	}

	if maxConcurrency <= 0 || maxConcurrency >= 500 {
		maxConcurrency = MaxConcurrentTests
	}

	outbounds := service.FromContext[adapter.OutboundManager](
		i.Context(),
	)

	results := make([]*URLTestResult, len(outboundTags))
	slots := make(chan struct{}, maxConcurrency)

	var wg sync.WaitGroup

	for idx, tag := range outboundTags {
		result := &URLTestResult{Tag: tag}
		results[idx] = result

		if err := ctx.Err(); err != nil {
			result.Error = err
			URLReporter.AddResult(result)
			continue
		}

		if !acquireTestSlot(ctx, slots) {
			result.Error = ctx.Err()
			URLReporter.AddResult(result)
			continue
		}

		wg.Add(1)

		go func() {
			defer wg.Done()
			defer func() { <-slots }()

			outbound, found := outbounds.Outbound(tag)
			if !found {
				result.Error = fmt.Errorf(
					"outbound %q not found", tag,
				)
				URLReporter.AddResult(result)
				return
			}

			transport := &http.Transport{
				DialContext: func(
					dialCtx context.Context,
					network string,
					addr string,
				) (net.Conn, error) {
					return outbound.DialContext(
						dialCtx,
						network,
						metadata.ParseSocksaddr(addr),
					)
				},
			}
			defer transport.CloseIdleConnections()

			client := &http.Client{
				Transport: transport,
				Timeout:   timeout,
			}

			result.Duration, result.Error = urlTest(
				ctx, client, url,
			)

			if result.Error == nil &&
				twice &&
				ctx.Err() == nil {
				result.Duration, result.Error = urlTest(
					ctx, client, url,
				)
			}

			if err := ctx.Err(); err != nil {
				result.Error = err
			}

			URLReporter.AddResult(result)
		}()
	}

	wg.Wait()
	return results
}

func urlTest(ctx context.Context, client *http.Client, url string) (time.Duration, error) {
	begin := time.Now()
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return 0, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return 0, err
	}
	_ = resp.Body.Close()
	return time.Since(begin), nil
}
