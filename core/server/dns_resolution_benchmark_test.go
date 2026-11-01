package main

import (
	"context"
	"fmt"
	"net"
	"net/netip"
	"sync/atomic"
	"testing"
	"time"

	"github.com/miekg/dns"
	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing/service"
)

func BenchmarkPR45DNSResolution(b *testing.B) {
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		b.Fatal(err)
	}

	port := listener.Addr().(*net.TCPAddr).Port
	var queries atomic.Int64

	handler := dns.HandlerFunc(func(w dns.ResponseWriter, r *dns.Msg) {
		msg := new(dns.Msg)
		msg.SetReply(r)
		msg.Authoritative = true

		if len(r.Question) == 1 {
			q := r.Question[0]

			if q.Name == "pr45.test." && q.Qtype == dns.TypeA {
				queries.Add(1)
				msg.Answer = []dns.RR{
					&dns.A{
						Hdr: dns.RR_Header{
							Name:   q.Name,
							Rrtype: dns.TypeA,
							Class:  dns.ClassINET,
							Ttl:    60,
						},
						A: net.IPv4(127, 0, 0, 1),
					},
				}
			} else {
				msg.Rcode = dns.RcodeNameError
			}
		}

		_ = w.WriteMsg(msg)
	})

	ready := make(chan struct{})
	serverErrors := make(chan error, 1)

	dnsServer := &dns.Server{
		Listener: listener,
		Handler:  handler,

		NotifyStartedFunc: func() {
			close(ready)
		},
	}

	go func() {
		serverErrors <- dnsServer.ActivateAndServe()
	}()

	select {
	case <-ready:
		// Server is listening.

	case err := <-serverErrors:
		_ = listener.Close()
		b.Fatalf("DNS server startup failed: %v", err)

	case <-time.After(5 * time.Second):
		_ = listener.Close()
		b.Fatal("DNS server startup timeout")
	}

	b.Cleanup(func() {
		if err := dnsServer.Shutdown(); err != nil {
			b.Errorf("DNS shutdown: %v", err)
		}

		if err := <-serverErrors; err != nil {
			b.Errorf("DNS server stopped with error: %v", err)
		}
	})

	// Verify that the DNS server is reachable before
	// measuring anything.
	probe := new(dns.Msg)
	probe.SetQuestion("pr45.test.", dns.TypeA)

	client := &dns.Client{
		Net:     "tcp",
		Timeout: 2 * time.Second,
	}

	probeResult, _, err := client.Exchange(
		probe,
		listener.Addr().String(),
	)

	if err != nil {
		b.Fatalf("DNS TCP preflight failed: %v", err)
	}

	if probeResult == nil || len(probeResult.Answer) != 1 {
		b.Fatalf("Unexpected DNS response: %v", probeResult)
	}

	expectedIP := netip.MustParseAddr("127.0.0.1")

	for _, tc := range []struct {
		name     string
		disabled bool
	}{
		{"CacheEnabled", false},
		{"CacheDisabled", true},
	} {
		b.Run(tc.name, func(b *testing.B) {
			config := fmt.Sprintf(`{
				"log": {"level": "error"},
				"dns": {
					"servers": [{
						"type": "tcp",
						"tag": "local",
						"server": "127.0.0.1",
						"server_port": %d
					}],
					"final": "local",
					"disable_cache": %t
				},
				"outbounds": [{
					"type": "direct",
					"tag": "direct"
				}],
				"route": {"final": "direct"}
			}`, port, tc.disabled)

			before := queries.Load()

			b.ReportAllocs()
			b.ResetTimer()

			for i := 0; i < b.N; i++ {
				rt, err := newTemporaryTestRuntime(
					config, false, "",
				)
				if err != nil {
					b.Fatal(err)
				}

				resolver := service.FromContext[adapter.DNSRouter](
					rt.box.Context(),
				)
				if resolver == nil {
					_ = rt.Close()
					b.Fatal("DNS router unavailable")
				}

				// Two lookups within the SAME runtime.
				for n := 0; n < 2; n++ {
					addresses, err := resolver.Lookup(
						context.Background(),
						"pr45.test",
						adapter.DNSQueryOptions{
							Strategy: C.DomainStrategyIPv4Only,
						},
					)

					if err != nil ||
						len(addresses) != 1 ||
						addresses[0] != expectedIP {
						_ = rt.Close()
						b.Fatalf(
							"lookup %d: addresses=%v err=%v",
							n, addresses, err,
						)
					}
				}

				if err := rt.Close(); err != nil {
					b.Fatal(err)
				}
			}

			b.StopTimer()

			total := queries.Load() - before

			b.ReportMetric(
				float64(total)/float64(b.N),
				"dns_queries/op",
			)
		})
	}
}
