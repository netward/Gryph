package main

import (
	"context"
	"testing"

	"GryphCore/gen"
	"GryphCore/test_utils"
)

// Measures the full temporary URL Test RPC with
// DNS cache enabled and disabled.
func BenchmarkPR45TemporaryURLRPCDNSCache(b *testing.B) {
	srv := newPR4BenchmarkServer(b)
	s := &server{}

	test_utils.ResetTestContext()
	b.Cleanup(test_utils.ResetTestContext)

	for _, tc := range []struct {
		name         string
		disableCache bool
	}{
		{"CacheEnabled", false},
		{"CacheDisabled", true},
	} {
		b.Run(tc.name, func(b *testing.B) {
			config := pr45DNSCacheConfig(b, tc.disableCache)

			req := &gen.TestReq{
				Config:             To(config),
				TestCurrent:        To(false),
				UseDefaultOutbound: To(true),
				Url:                To(srv.URL),
				MaxConcurrency:     To(int32(1)),
				TestTimeoutMs:      To(int32(3000)),
			}

			b.ReportAllocs()
			b.ResetTimer()

			for i := 0; i < b.N; i++ {
				resp, err := s.Test(context.Background(), req)
				if err != nil {
					b.Fatal(err)
				}

				if resp == nil || len(resp.GetResults()) != 1 {
					b.Fatalf("unexpected response: %v", resp)
				}
				if msg := resp.GetResults()[0].GetError(); msg != "" {
					b.Fatalf("URL Test failed: %s", msg)
				}

				b.StopTimer()
				_ = test_utils.URLReporter.Results()
				b.StartTimer()
			}
		})
	}
}
