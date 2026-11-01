package main

import (
	"context"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/http/httptrace"
	"sync"
	"testing"
	"time"
)

type pr4ConnTimes struct {
	accepted time.Time
	active   time.Time
	handler  time.Time
}

func TestPR4HTTPConnectionTrace(t *testing.T) {
	var mu sync.Mutex
	records := make(map[string]*pr4ConnTimes)

	getRecord := func(key string) *pr4ConnTimes {
		r := records[key]
		if r == nil {
			r = &pr4ConnTimes{}
			records[key] = r
		}
		return r
	}

	srv := httptest.NewUnstartedServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			mu.Lock()
			getRecord(r.RemoteAddr).handler = time.Now()
			mu.Unlock()

			w.Header().Set("Content-Length", "2")
			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte("OK"))
		}),
	)

	srv.Config.ConnState = func(
		conn net.Conn,
		state http.ConnState,
	) {
		now := time.Now()
		key := conn.RemoteAddr().String()

		mu.Lock()
		defer mu.Unlock()

		record := getRecord(key)

		switch state {
		case http.StateNew:
			record.accepted = now

		case http.StateActive:
			record.active = now
		}
	}

	srv.Start()
	defer srv.Close()

	for _, tc := range []struct {
		name  string
		reuse bool
	}{
		{"Fresh", false},
		{"KeepAlive", true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var transport *http.Transport

			if tc.reuse {
				transport = &http.Transport{}
				defer transport.CloseIdleConnections()
			}

			for i := 0; i < 8; i++ {
				if !tc.reuse {
					transport = &http.Transport{}
				}

				var traceMu sync.Mutex
				var wroteAt time.Time
				var localAddr string
				var reused bool

				trace := &httptrace.ClientTrace{
					GotConn: func(info httptrace.GotConnInfo) {
						traceMu.Lock()
						localAddr = info.Conn.LocalAddr().String()
						reused = info.Reused
						traceMu.Unlock()
					},

					WroteRequest: func(
						info httptrace.WroteRequestInfo,
					) {
						traceMu.Lock()
						wroteAt = time.Now()
						traceMu.Unlock()
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

				_, readErr := io.Copy(io.Discard, resp.Body)
				closeErr := resp.Body.Close()
				finished := time.Now()

				if readErr != nil || closeErr != nil {
					t.Fatalf(
						"read=%v close=%v",
						readErr, closeErr,
					)
				}

				if resp.StatusCode != http.StatusOK {
					t.Fatalf("status=%d", resp.StatusCode)
				}

				traceMu.Lock()
				addr := localAddr
				wrote := wroteAt
				wasReused := reused
				traceMu.Unlock()

				mu.Lock()
				record, ok := records[addr]
				var times pr4ConnTimes
				if ok {
					times = *record
				}
				mu.Unlock()

				if !ok ||
					wrote.IsZero() ||
					times.active.IsZero() ||
					times.handler.IsZero() {
					t.Fatalf(
						"missing trace events for %q",
						addr,
					)
				}

				var acceptToActive time.Duration
				if !wasReused && !times.accepted.IsZero() {
					acceptToActive = times.active.Sub(
						times.accepted,
					)
				}

				t.Logf(
					"%s #%d reused=%t "+
						"acceptAt=%v "+
						"acceptToActive=%v "+
						"writeToActive=%v "+
						"activeToHandler=%v "+
						"total=%v",
					tc.name,
					i+1,
					wasReused,
					times.accepted.Sub(start),
					acceptToActive,
					times.active.Sub(wrote),
					times.handler.Sub(times.active),
					finished.Sub(start),
				)

				if !tc.reuse {
					transport.CloseIdleConnections()
				}
			}
		})
	}
}

