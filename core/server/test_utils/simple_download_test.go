package test_utils

import (
	"bytes"
	"context"
	"errors"
	"net"
	"net/http"
	"net/http/httptest"
	"strconv"
	"testing"
	"time"
)

func TestSimpleDownloadSuccess(t *testing.T) {
	payload := bytes.Repeat([]byte("x"), 256*1024)

	server := httptest.NewServer(
		http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			t.Log("HTTP handler: request received")

			w.Header().Set(
				"Content-Length",
				strconv.Itoa(len(payload)),
			)

			n, err := w.Write(payload)
			t.Logf(
				"HTTP handler: wrote=%d expected=%d err=%v",
				n, len(payload), err,
			)
		}),
	)
	defer server.Close()

	ctx, cancel := context.WithTimeout(
		context.Background(), 5*time.Second,
	)
	defer cancel()

	dialer := func(
		ctx context.Context,
		network, address string,
	) (net.Conn, error) {
		t.Logf("DialContext: %s %s", network, address)

		conn, err := (&net.Dialer{
			Timeout: 2 * time.Second,
		}).DialContext(ctx, network, address)

		t.Logf("DialContext: err=%v", err)
		return conn, err
	}

	var result SpeedTestResult

	started := time.Now()

	err := simpleDownloadTest(
		ctx, dialer, &result, server.URL, 5*time.Second,
	)

	t.Logf(
		"Result: elapsed=%v latency=%d speed=%q cancelled=%v error=%v",
		time.Since(started),
		result.Latency,
		result.DlSpeed,
		result.Cancelled,
		err,
	)

	if err != nil {
		t.Fatal(err)
	}
	if result.DlSpeed == "" {
		t.Fatal("download speed was not calculated")
	}
	if result.Cancelled {
		t.Fatal("successful download marked as cancelled")
	}

	_, running := SpTQuerier.Result()
	if running {
		t.Fatal("speed test remains active")
	}
}

func TestSimpleDownloadCancellation(t *testing.T) {
	started := make(chan struct{})
	handlerDone := make(chan struct{})

	server := httptest.NewServer(
		http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			defer close(handlerDone)

			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte("initial data"))
			w.(http.Flusher).Flush()

			close(started)
			<-r.Context().Done()
		}),
	)
	defer server.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	var result SpeedTestResult
	done := make(chan error, 1)

	go func() {
		done <- simpleDownloadTest(
			ctx,
			(&net.Dialer{}).DialContext,
			&result,
			server.URL,
			5*time.Second,
		)
	}()

	select {
	case <-started:
	case <-time.After(5 * time.Second):
		t.Fatal("HTTP request did not start")
	}

	cancel()

	select {
	case err := <-done:
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("expected cancellation, got %v", err)
		}

	case <-time.After(5 * time.Second):
		t.Fatal("download did not stop after cancellation")
	}

	select {
	case <-handlerDone:
	case <-time.After(5 * time.Second):
		t.Fatal("HTTP handler did not terminate")
	}

	if !result.Cancelled {
		t.Fatal("cancelled download was not marked as cancelled")
	}

	_, running := SpTQuerier.Result()
	if running {
		t.Fatal("speed test remains active")
	}
}
