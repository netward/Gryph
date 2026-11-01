package main

import (
	"context"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing/common/metadata"
	"github.com/sagernet/sing/service"
)

func BenchmarkPR4HTTPPath(b *testing.B) {
	srv := httptest.NewServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			w.Header().Set(
				"Content-Type",
				"text/plain",
			)
			w.Header().Set(
				"Content-Length",
				"2",
			)

			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte("OK"))
		}),
	)
	b.Cleanup(srv.Close)

	rt, err := newTemporaryTestRuntime(
		lifecycleTestConfig,
		false,
		"",
	)
	if err != nil {
		b.Fatal(err)
	}

	b.Cleanup(func() {
		if err := rt.Close(); err != nil {
			b.Errorf("Close: %v", err)
		}
	})

	manager := service.FromContext[adapter.OutboundManager](
		rt.box.Context(),
	)

	outbound, ok := manager.Outbound("direct")
	if !ok {
		b.Fatal("direct outbound not found")
	}

	cases := []struct {
		name      string
		singbox   bool
		keepAlive bool
	}{
		{"StdlibFresh", false, false},
		{"SingBoxFresh", true, false},
		{"StdlibKeepAlive", false, true},
		{"SingBoxKeepAlive", true, true},
	}

	for _, tc := range cases {
		b.Run(tc.name, func(b *testing.B) {
			newTransport := func() *http.Transport {
				transport := &http.Transport{
					// Leave HTTP keep-alive enabled.
					// Fresh cases use a new transport
					// for each request instead.
					DisableKeepAlives: false,
				}

				if tc.singbox {
					transport.DialContext = func(
						ctx context.Context,
						network string,
						address string,
					) (net.Conn, error) {
						return outbound.DialContext(
							ctx,
							network,
							metadata.ParseSocksaddr(address),
						)
					}
				}

				return transport
			}

			var transport *http.Transport
			var client *http.Client

			if tc.keepAlive {
				transport = newTransport()

				client = &http.Client{
					Transport: transport,
					Timeout:   5 * time.Second,
				}
			}

			b.ReportAllocs()
			b.ResetTimer()

			for i := 0; i < b.N; i++ {
				if !tc.keepAlive {
					transport = newTransport()

					client = &http.Client{
						Transport: transport,
						Timeout:   5 * time.Second,
					}
				}

				resp, err := client.Get(srv.URL)
				if err != nil {
					b.Fatalf(
						"%s request %d: %v",
						tc.name,
						i,
						err,
					)
				}

				data, readErr := io.ReadAll(resp.Body)
				closeErr := resp.Body.Close()

				if readErr != nil {
					b.Fatal(readErr)
				}
				if closeErr != nil {
					b.Fatal(closeErr)
				}

				if resp.StatusCode != http.StatusOK {
					b.Fatalf(
						"unexpected status: %d",
						resp.StatusCode,
					)
				}

				if string(data) != "OK" {
					b.Fatalf(
						"unexpected body: %q",
						data,
					)
				}

				if !tc.keepAlive {
					transport.CloseIdleConnections()
				}
			}

			b.StopTimer()

			if transport != nil {
				transport.CloseIdleConnections()
			}
		})
	}
}

