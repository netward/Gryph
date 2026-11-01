package main

import (
	"bytes"
	"errors"
	"fmt"
	"net"
	"net/netip"
	"reflect"
	"runtime"
	"testing"
	"unsafe"

	"github.com/sagernet/sing/common/control"
	"golang.org/x/sys/windows"
)

// Experimental implementation only.
// No production runtime behavior is changed.

func pr44WindowsSnapshot() ([]control.Interface, error) {
	const (
		initialBuffer = 15000
		maxBuffer     = 16 << 20
		maxAttempts   = 5

		flags = windows.GAA_FLAG_INCLUDE_PREFIX |
			windows.GAA_FLAG_INCLUDE_GATEWAYS
	)

	size := uint32(initialBuffer)

	for attempt := 0; attempt < maxAttempts; attempt++ {
		buffer := make([]byte, size)

		first := (*windows.IpAdapterAddresses)(
			unsafe.Pointer(&buffer[0]),
		)

		needed := size

		err := windows.GetAdaptersAddresses(
			uint32(windows.AF_UNSPEC),
			flags,
			0,
			first,
			&needed,
		)

		if errors.Is(err, windows.ERROR_BUFFER_OVERFLOW) {
			if needed <= size || needed > maxBuffer {
				return nil, fmt.Errorf(
					"invalid adapter buffer size: %d",
					needed,
				)
			}

			size = needed
			continue
		}

		if err != nil {
			return nil, fmt.Errorf(
				"GetAdaptersAddresses: %w",
				err,
			)
		}

		if needed == 0 {
			return nil, nil
		}

		var interfaces []control.Interface

		for aa := first; aa != nil; aa = aa.Next {
			// Match Go's net/interface_windows.go.
			index := aa.IfIndex
			if index == 0 {
				index = aa.Ipv6IfIndex
			}

			iface := control.Interface{
				Index: int(index),
				Name: windows.UTF16PtrToString(
					aa.FriendlyName,
				),
			}

			// Match the flags reported by net.Interfaces.
			if aa.OperStatus == windows.IfOperStatusUp {
				iface.Flags |= net.FlagUp
				iface.Flags |= net.FlagRunning
			}

			switch aa.IfType {
			case windows.IF_TYPE_ETHERNET_CSMACD,
				windows.IF_TYPE_ISO88025_TOKENRING,
				windows.IF_TYPE_IEEE80211,
				windows.IF_TYPE_IEEE1394:

				iface.Flags |= net.FlagBroadcast
				iface.Flags |= net.FlagMulticast

			case windows.IF_TYPE_PPP,
				windows.IF_TYPE_TUNNEL:

				iface.Flags |= net.FlagPointToPoint
				iface.Flags |= net.FlagMulticast

			case windows.IF_TYPE_SOFTWARE_LOOPBACK:

				iface.Flags |= net.FlagLoopback
				iface.Flags |= net.FlagMulticast

			case windows.IF_TYPE_ATM:

				iface.Flags |= net.FlagBroadcast
				iface.Flags |= net.FlagPointToPoint
				iface.Flags |= net.FlagMulticast
			}

			if aa.Mtu == ^uint32(0) {
				iface.MTU = -1
			} else {
				iface.MTU = int(aa.Mtu)
			}

			if aa.PhysicalAddressLength > 0 {
				n := int(aa.PhysicalAddressLength)

				if n > len(aa.PhysicalAddress) {
					return nil, fmt.Errorf(
						"invalid hardware address length: %d",
						n,
					)
				}

				iface.HardwareAddr = make(
					net.HardwareAddr,
					n,
				)

				copy(
					iface.HardwareAddr,
					aa.PhysicalAddress[:n],
				)
			}

			// Read unicast addresses directly from
			// this adapter's Windows snapshot.
			for addr := aa.FirstUnicastAddress; addr != nil; addr = addr.Next {

				ip := addr.Address.IP()
				if ip == nil {
					continue
				}

				ipAddr, ok := netip.AddrFromSlice(ip)
				if !ok {
					continue
				}

				ipAddr = ipAddr.Unmap()

				prefix := netip.PrefixFrom(
					ipAddr,
					int(addr.OnLinkPrefixLength),
				)

				iface.Addresses = append(
					iface.Addresses,
					prefix,
				)
			}

			// Preserve the legacy conversion behavior:
			// InterfaceFromNet treats net.IPAddr
			// (anycast) as an invalid netip.Prefix.
			for addr := aa.FirstAnycastAddress; addr != nil; addr = addr.Next {

				if addr.Address.IP() != nil {
					iface.Addresses = append(
						iface.Addresses,
						netip.Prefix{},
					)
				}
			}

			interfaces = append(interfaces, iface)
		}

		// The returned pointers reference buffer memory.
		runtime.KeepAlive(buffer)

		return interfaces, nil
	}

	return nil, errors.New(
		"network adapters changed during enumeration",
	)
}

// Compare the experimental snapshot with the
// existing sing DefaultInterfaceFinder implementation.
func TestPR44WindowsSnapshotEquivalent(t *testing.T) {
	for iteration := 0; iteration < 3; iteration++ {
		expected, err := pr44LegacySnapshot()
		if err != nil {
			t.Fatal(err)
		}

		actual, err := pr44WindowsSnapshot()
		if err != nil {
			t.Fatalf(
				"Windows snapshot: %v",
				err,
			)
		}

		if len(actual) != len(expected) {
			t.Fatalf(
				"iteration %d: adapter count "+
					"legacy=%d optimized=%d",
				iteration,
				len(expected),
				len(actual),
			)
		}

		for i := range expected {
			want := expected[i]
			got := actual[i]

			if want.Index != got.Index ||
				want.Name != got.Name ||
				want.MTU != got.MTU ||
				want.Flags != got.Flags ||
				!bytes.Equal(
					want.HardwareAddr,
					got.HardwareAddr,
				) {

				t.Errorf(
					"adapter %d mismatch:\n"+
						"legacy: %#v\noptimized: %#v",
					i,
					want,
					got,
				)

				continue
			}

			if !reflect.DeepEqual(
				want.Addresses,
				got.Addresses,
			) {
				t.Errorf(
					"adapter %q addresses differ:\n"+
						"legacy: %v\noptimized: %v",
					want.Name,
					want.Addresses,
					got.Addresses,
				)
			}
		}
	}
}

// Existing implementation: one interfaces call,
// followed by one Addrs call per adapter.
func BenchmarkPR44WindowsLegacyFinderUpdate(b *testing.B) {
	b.ReportAllocs()
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		if _, err := pr44LegacySnapshot(); err != nil {
			b.Fatal(err)
		}
	}
}


// Experimental implementation: one complete
// GetAdaptersAddresses snapshot per update,
// except when buffer resizing requires retries.
func BenchmarkPR44WindowsSnapshot(b *testing.B) {
	b.ReportAllocs()
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		if _, err := pr44WindowsSnapshot(); err != nil {
			b.Fatal(err)
		}
	}
}


func pr44LegacySnapshot() ([]control.Interface, error) {
	netIfs, err := net.Interfaces()
	if err != nil {
		return nil, err
	}

	interfaces := make(
		[]control.Interface,
		0,
		len(netIfs),
	)

	for _, netIf := range netIfs {
		iface, err := control.InterfaceFromNet(netIf)
		if err != nil {
			return nil, err
		}

		interfaces = append(interfaces, iface)
	}

	return interfaces, nil
}