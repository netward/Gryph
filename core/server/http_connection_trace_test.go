package main

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"net/http/httptrace"
	"sync"
	"testing"
	"time"
)

func TestPR4HTTPConnectionTrace(t *testing.T) {
	srv := httptest.NewServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			w.Header().Set("Content-Length", "2")
			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte("OK"))
		}),
	)
	defer srv.Close()

	cases := []struct {
		name  string
		reuse bool
	}{
		{"Fresh", false},
		{"KeepAlive", true},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			var sharedTransport *http.Transport

			if tc.reuse {
				sharedTransport = &http.Transport{}
				defer sharedTransport.CloseIdleConnections()
			}

			for i := 0; i < 8; i++ {
				transport := sharedTransport
				if !tc.reuse {
					transport = &http.Transport{}
				}

				var mu sync.Mutex
				var connectStart time.Time
				var connectDone time.Time
				var firstByte time.Time
				var reused bool

				trace := &httptrace.ClientTrace{
					ConnectStart: func(_, _ string) {
						mu.Lock()
						connectStart = time.Now()
						mu.Unlock()
					},

					ConnectDone: func(
						_, _ string,
						_ error,
					) {
						mu.Lock()
						connectDone = time.Now()
						mu.Unlock()
					},

					GotConn: func(
						info httptrace.GotConnInfo,
					) {
						mu.Lock()
						reused = info.Reused
						mu.Unlock()
					},

					GotFirstResponseByte: func() {
						mu.Lock()
						firstByte = time.Now()
						mu.Unlock()
					},
				}

				ctx := httptrace.WithClientTrace(
					context.Background(),
					trace,
				)

				req, err := http.NewRequestWithContext(
					ctx,
					http.MethodGet,
					srv.URL,
					nil,
				)
				if err != nil {
					t.Fatal(err)
				}

				client := &http.Client{
					Transport: transport,
					Timeout:   5 * time.Second,
				}

				start := time.Now()

				resp, err := client.Do(req)
				if err != nil {
					t.Fatal(err)
				}

				responseReceived := time.Now()

				_, readErr := io.Copy(
					io.Discard,
					resp.Body,
				)
				closeErr := resp.Body.Close()

				if readErr != nil {
					t.Fatal(readErr)
				}
				if closeErr != nil {
					t.Fatal(closeErr)
				}

				bodyDone := time.Now()

				if !tc.reuse {
					transport.CloseIdleConnections()
				}

				cleanupDone := time.Now()

				mu.Lock()

				var dialDuration time.Duration
				if !connectStart.IsZero() &&
					!connectDone.IsZero() {
					dialDuration = connectDone.Sub(
						connectStart,
					)
				}

				var firstByteDuration time.Duration
				if !firstByte.IsZero() {
					firstByteDuration = firstByte.Sub(start)
				}

				wasReused := reused
				mu.Unlock()

				t.Logf(
					"request=%d reused=%t "+
						"dial=%v firstByte=%v "+
						"headers=%v body=%v cleanup=%v total=%v",
					i+1,
					wasReused,
					dialDuration,
					firstByteDuration,
					responseReceived.Sub(start),
					bodyDone.Sub(responseReceived),
					cleanupDone.Sub(bodyDone),
					cleanupDone.Sub(start),
				)
			}
		})
	}
}
