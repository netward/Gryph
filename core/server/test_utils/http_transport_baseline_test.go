package test_utils

import (
	"bytes"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strconv"
	"testing"
	"time"
)

func TestHTTPTransportBaseline(t *testing.T) {
	payload := bytes.Repeat([]byte("x"), 256*1024)

	server := httptest.NewServer(
		http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set(
				"Content-Length",
				strconv.Itoa(len(payload)),
			)

			_, _ = w.Write(payload)
		}),
	)
	defer server.Close()

	cases := []struct {
		name   string
		client *http.Client
	}{
		{
			name: "default transport",
			client: &http.Client{
				Timeout: 5 * time.Second,
			},
		},
		{
			name: "custom dial only",
			client: &http.Client{
				Transport: &http.Transport{
					DialContext: (&net.Dialer{}).DialContext,
				},
				Timeout: 5 * time.Second,
			},
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			start := time.Now()

			resp, err := tc.client.Get(server.URL)
			if err != nil {
				t.Fatal(err)
			}
			defer resp.Body.Close()

			var counter atomicByteCounter

			n, err := io.Copy(&counter, resp.Body)

			t.Logf(
				"status=%s contentLength=%d received=%d counted=%d elapsed=%v error=%v",
				resp.Status,
				resp.ContentLength,
				n,
				counter.total.Load(),
				time.Since(start),
				err,
			)

			if err != nil {
				t.Fatal(err)
			}

			if n != int64(len(payload)) {
				t.Fatalf(
					"incomplete download: got %d, expected %d",
					n, len(payload),
				)
			}
		})
	}
}
