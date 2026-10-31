package test_utils

import (
	"bytes"
	"context"
	"errors"
	"net"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestSimpleDownloadSuccess(t *testing.T) {
	payload := bytes.Repeat([]byte("x"), 256*1024)

	server := httptest.NewServer(
		http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			_, _ = w.Write(payload)
		}),
	)
	defer server.Close()

	ctx, cancel := context.WithTimeout(
		context.Background(), 5*time.Second,
	)
	defer cancel()

	var result SpeedTestResult
	dialer := (&net.Dialer{}).DialContext

	err := simpleDownloadTest(
		ctx, dialer, &result, server.URL, 5*time.Second,
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
