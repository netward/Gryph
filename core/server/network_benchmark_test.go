package main

import (
	"net"
	"runtime"
	"testing"
)

// Measures Windows network interface enumeration.
func BenchmarkPR4WindowsNetworkInterfaces(b *testing.B) {
	if runtime.GOOS != "windows" {
		b.Skip("Windows-only benchmark")
	}

	b.ReportAllocs()
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		_, err := net.Interfaces()
		if err != nil {
			b.Fatal(err)
		}
	}
}

// Measures address enumeration for all detected adapters.
func BenchmarkPR4WindowsInterfaceAddresses(b *testing.B) {
	if runtime.GOOS != "windows" {
		b.Skip("Windows-only benchmark")
	}

	interfaces, err := net.Interfaces()
	if err != nil {
		b.Fatal(err)
	}

	if len(interfaces) == 0 {
		b.Skip("No network interfaces found")
	}

	b.ReportAllocs()
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		for _, iface := range interfaces {
			_, err := iface.Addrs()
			if err != nil {
				b.Fatalf(
					"adapter %q: %v",
					iface.Name,
					err,
				)
			}
		}
	}
}
