package test_utils

import (
	"GryphCore/internal/boxbox"
	"context"
	"encoding/json"
	"fmt"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing/common/metadata"
	"github.com/sagernet/sing/service"
	"io"
	"net"
	"net/http"
	"sync"
	"time"
)

type IPInfo struct {
	IP          string `json:"ip"`
	CountryCode string `json:"country_code"`
}

var IPReporter IPTestReporter

const IPTestTimeout = 3 * time.Second
const ipInfoAPI = "https://api.ip2location.io/"

type IPTestResult struct {
	Result IPInfo
	Tag    string
	Error  error
}

type IPTestReporter struct {
	results []*IPTestResult
	mu      sync.Mutex
}

func (u *IPTestReporter) AddResult(result *IPTestResult) {
	u.mu.Lock()
	defer u.mu.Unlock()
	u.results = append(u.results, result)
}

func (u *IPTestReporter) Results() []*IPTestResult {
	u.mu.Lock()
	defer u.mu.Unlock()
	res := u.results
	u.results = nil
	return res
}

func BatchIPTest(
	ctx context.Context,
	i *boxbox.Box,
	outboundTags []string,
	maxConcurrency int,
	timeout time.Duration,
) []*IPTestResult {
	if timeout <= 0 {
		timeout = IPTestTimeout
	}

	if maxConcurrency <= 0 || maxConcurrency >= 500 {
		maxConcurrency = MaxConcurrentTests
	}

	outbounds := service.FromContext[adapter.OutboundManager](
		i.Context(),
	)

	results := make([]*IPTestResult, len(outboundTags))
	slots := make(chan struct{}, maxConcurrency)

	var wg sync.WaitGroup

	for idx, tag := range outboundTags {
		result := &IPTestResult{Tag: tag}
		results[idx] = result

		if err := ctx.Err(); err != nil {
			result.Error = err
			IPReporter.AddResult(result)
			continue
		}

		if !acquireTestSlot(ctx, slots) {
			result.Error = ctx.Err()
			IPReporter.AddResult(result)
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
				IPReporter.AddResult(result)
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

			result.Result, result.Error = ipTest(
				ctx, client,
			)

			if err := ctx.Err(); err != nil {
				result.Error = err
			}

			IPReporter.AddResult(result)
		}()
	}

	wg.Wait()
	return results
}

func ipTest(ctx context.Context, client *http.Client) (IPInfo, error) {
	var res IPInfo
	req, err := http.NewRequestWithContext(ctx, "GET", ipInfoAPI, nil)
	if err != nil {
		return res, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return res, err
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return res, err
	}
	err = json.Unmarshal(data, &res)
	if err != nil {
		return res, err
	}
	return res, nil
}
