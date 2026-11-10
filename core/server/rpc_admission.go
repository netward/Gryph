package main

const (
	// Maximum concurrent RPC handlers per category.
	maxRegularRPC   = 16
	maxControlRPC   = 2
	maxTelemetryRPC = 2
)

// rpcAdmission limits concurrent RPC handlers.
//
// Each category owns independent capacity.
// Regular requests cannot consume control slots.
type rpcAdmission struct {
	regular   chan struct{}
	control   chan struct{}
	telemetry chan struct{}
}

func newRPCAdmission() *rpcAdmission {
	return &rpcAdmission{
		regular: make(
			chan struct{},
			maxRegularRPC,
		),

		control: make(
			chan struct{},
			maxControlRPC,
		),

		telemetry: make(
			chan struct{},
			maxTelemetryRPC,
		),
	}
}

// tryAcquire attempts to reserve one RPC slot.
//
// It does not wait for an available slot.
//
// On success, release must be called exactly once
// after request processing and response writing.
func (a *rpcAdmission) tryAcquire(
	method string,
) (release func(), ok bool) {

	slots := a.regular

	switch method {

	// Control operations must remain available
	// during regular RPC saturation.
	case "Stop",
		"StopTest",
		"SetSystemDNS":

		slots = a.control

	// Monitoring and result queries.
	case "QueryStats",
		"ListConnections",
		"QueryURLTest",
		"QueryIPTest",
		"QuerySpeedTest",
		"QueryCountryTest":

		slots = a.telemetry
	}

	select {
	case slots <- struct{}{}:

		return func() {
			<-slots
		}, true

	default:
		// No free capacity.
		return nil, false
	}
}
