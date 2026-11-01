package main

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"net/http/httptrace"
	"strconv"
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
			// Capture the time at which the HTTP
			// handler receives the request.
			w.Header().Set(
				"X-Handler-Time",
				strconv.FormatInt(
					time.Now().UnixNano(),
					10,
				),
			)

			w.Header().Set("Content-Length", "2")
			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte("OK"))
		}),
	)
	defer srv.Close()

	for _, tc := range []struct {
		name  string
		reuse bool
	}{
		{"Fresh", false},
		{"KeepAlive", true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var shared *http.Transport

			if tc.reuse {
				shared = &http.Transport{}
				defer shared.CloseIdleConnections()
			}

			for i := 0; i < 8; i++ {
				transport := shared
				if transport == nil {
					transport = &http.Transport{}
				}

				var mu sync.Mutex
				var connectStart time.Time
				var connectDone time.Time
				var wroteAt time.Time
				var firstByteAt time.Time
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

					WroteRequest: func(
						info httptrace.WroteRequestInfo,
					) {
						mu.Lock()
						wroteAt = time.Now()
						mu.Unlock()
					},

					GotFirstResponseByte: func() {
						mu.Lock()
						firstByteAt = time.Now()
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

				_, readErr := io.Copy(
					io.Discard,
					resp.Body,
				)
				closeErr := resp.Body.Close()

				if readErr != nil || closeErr != nil {
					t.Fatalf(
						"read=%v close=%v",
						readErr, closeErr,
					)
				}

				finished := time.Now()

				raw := resp.Header.Get("X-Handler-Time")
				serverNano, err := strconv.ParseInt(
					raw, 10, 64,
				)
				if err != nil {
					t.Fatal(err)
				}
				serverAt := time.Unix(0, serverNano)

				mu.Lock()
				dialStart := connectStart
				dialEnd := connectDone
				wrote := wroteAt
				first := firstByteAt
				wasReused := reused
				mu.Unlock()

				if wrote.IsZero() || first.IsZero() {
					t.Fatal("missing HTTP trace event")
				}

				var dial time.Duration
				if !dialStart.IsZero() &&
					!dialEnd.IsZero() {
					dial = dialEnd.Sub(dialStart)
				}

				t.Logf(
					"%s #%d reused=%t "+
						"tcp=%v send=%v "+
						"writeToHandler=%v "+
						"handlerToFirstByte=%v "+
						"total=%v",
					tc.name,
					i+1,
					wasReused,
					dial,
					wrote.Sub(start),
					serverAt.Sub(wrote),
					first.Sub(serverAt),
					finished.Sub(start),
				)

				if !tc.reuse {
					transport.CloseIdleConnections()
				}
			}
		})
	}
}
