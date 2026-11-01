package main

import (
	"encoding/json"
	"testing"
)

// Builds a benchmark configuration with an explicit
// DNS cache setting. All other options are identical.
func pr45DNSCacheConfig(
	b *testing.B,
	disableCache bool,
) string {
	b.Helper()

	var config map[string]any

	if err := json.Unmarshal(
		[]byte(lifecycleTestConfig),
		&config,
	); err != nil {
		b.Fatal(err)
	}

	dnsOptions, ok := config["dns"].(map[string]any)
	if !ok {
		dnsOptions = make(map[string]any)
		config["dns"] = dnsOptions
	}

	dnsOptions["disable_cache"] = disableCache

	result, err := json.Marshal(config)
	if err != nil {
		b.Fatal(err)
	}

	return string(result)
}

// A/B benchmark of temporary sing-box lifecycle.
//
// CacheEnabled  - standard DNS cache behavior
// CacheDisabled - DNS cache allocation disabled
func BenchmarkPR45TemporaryRuntimeDNSCache(b *testing.B) {
	cases := []struct {
		name         string
		disableCache bool
	}{
		{"CacheEnabled", false},
		{"CacheDisabled", true},
	}

	for _, tc := range cases {
		b.Run(tc.name, func(b *testing.B) {
			config := pr45DNSCacheConfig(
				b,
				tc.disableCache,
			)

			b.ReportAllocs()
			b.ResetTimer()

			for i := 0; i < b.N; i++ {
				rt, err := newTemporaryTestRuntime(
					config,
					false,
					"",
				)
				if err != nil {
					b.Fatal(err)
				}

				if err := rt.Close(); err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}
