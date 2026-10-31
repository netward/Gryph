package test_utils

import (
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"GryphCore/internal/boxbox"
	"GryphCore/internal/boxmain"
)

const batchTestConfig = `{
    "log": {"level": "error"},
    "outbounds": [{"type": "direct", "tag": "direct"}],
    "route": {"final": "direct"}
}`

func newBatchTestBox(t *testing.T) *boxbox.Box {
	t.Helper()

	instance, cancel, err := boxmain.Create(
		[]byte(batchTestConfig),
	)
	if err != nil {
		t.Fatal(err)
	}

	t.Cleanup(func() {
		cancel()
		if err := instance.Close(); err != nil {
			t.Errorf("close sing-box: %v", err)
		}
	})

	return instance
}

func TestBatchURLCancelWhileSlotFull(t *testing.T) {
	instance := newBatchTestBox(t)

	started := make(chan struct{}, 1)

	server := httptest.NewServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			select {
			case started <- struct{}{}:
			default:
			}

			<-r.Context().Done()
		}),
	)
	defer server.Close()

	ctx, cancel := context.WithCancel(
		context.Background(),
	)
	defer cancel()

	done := make(chan []*URLTestResult, 1)

	go func() {
		done <- BatchURLTest(
			ctx,
			instance,
			[]string{"direct", "direct", "direct"},
			server.URL,
			1,
			false,
			30*time.Second,
		)
	}()

	select {
	case <-started:
	case <-time.After(5 * time.Second):
		t.Fatal("first request did not start")
	}

	cancel()

	select {
	case results := <-done:
		if len(results) != 3 {
			t.Fatalf(
				"expected 3 results, got %d",
				len(results),
			)
		}

		for idx, result := range results {
			if result == nil {
				t.Fatalf("result %d is nil", idx)
			}

			if result.Tag != "direct" {
				t.Errorf(
					"result %d: unexpected tag %q",
					idx, result.Tag,
				)
			}

			if !errors.Is(
				result.Error,
				context.Canceled,
			) {
				t.Errorf(
					"result %d: expected cancellation, got %v",
					idx, result.Error,
				)
			}
		}

	case <-time.After(5 * time.Second):
		t.Fatal(
			"BatchURLTest blocked after cancellation",
		)
	}
}

func TestBatchURLMissingOutbound(t *testing.T) {
	instance := newBatchTestBox(t)

	results := BatchURLTest(
		context.Background(),
		instance,
		[]string{"missing", "missing"},
		"http://127.0.0.1/",
		1,
		false,
		time.Second,
	)

	if len(results) != 2 {
		t.Fatalf("expected 2 results, got %d", len(results))
	}

	for idx, result := range results {
		if result == nil || result.Error == nil {
			t.Fatalf(
				"result %d: expected outbound error",
				idx,
			)
		}
	}
}

func TestBatchIPCancelledAndMissingOutbound(t *testing.T) {
	instance := newBatchTestBox(t)

	ctx, cancel := context.WithCancel(
		context.Background(),
	)
	cancel()

	cancelled := BatchIPTest(
		ctx,
		instance,
		[]string{"direct", "direct"},
		1,
		time.Second,
	)

	if len(cancelled) != 2 {
		t.Fatalf(
			"expected 2 cancelled results, got %d",
			len(cancelled),
		)
	}

	for idx, result := range cancelled {
		if result == nil ||
			!errors.Is(result.Error, context.Canceled) {
			t.Fatalf(
				"result %d: expected cancellation, got %v",
				idx, result,
			)
		}
	}

	missing := BatchIPTest(
		context.Background(),
		instance,
		[]string{"missing", "missing"},
		1,
		time.Second,
	)

	if len(missing) != 2 {
		t.Fatalf(
			"expected 2 missing-outbound results, got %d",
			len(missing),
		)
	}

	for idx, result := range missing {
		if result == nil || result.Error == nil {
			t.Fatalf(
				"result %d: expected outbound error",
				idx,
			)
		}
	}
}
