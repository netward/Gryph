package main

import "testing"

// Regular RPC saturation must not block control RPCs.
func TestRPCAdmissionControlAvailableWhenRegularFull(
	t *testing.T,
) {
	a := newRPCAdmission()

	releases := make(
		[]func(),
		0,
		maxRegularRPC,
	)

	// Occupy every regular slot.
	for i := 0; i < maxRegularRPC; i++ {
		release, ok := a.tryAcquire("Test")

		if !ok {
			t.Fatalf(
				"regular slot %d was rejected",
				i,
			)
		}

		releases = append(releases, release)
	}

	// The next regular request must be rejected.
	if release, ok := a.tryAcquire("IPTest"); ok {
		release()

		t.Fatal(
			"regular limit was not enforced",
		)
	}

	// Control requests must remain available.
	releaseStopTest, ok :=
		a.tryAcquire("StopTest")

	if !ok {
		t.Fatal(
			"StopTest was blocked by regular RPC saturation",
		)
	}

	releaseStop, ok := a.tryAcquire("Stop")

	if !ok {
		t.Fatal(
			"Stop was blocked by regular RPC saturation",
		)
	}

	// Telemetry must also remain available.
	releaseStats, ok :=
		a.tryAcquire("QueryStats")

	if !ok {
		t.Fatal(
			"QueryStats was blocked by regular RPC saturation",
		)
	}

	releaseStopTest()
	releaseStop()
	releaseStats()

	// Release all regular slots.
	for _, release := range releases {
		release()
	}

	// Capacity must become available again.
	release, ok := a.tryAcquire("Test")

	if !ok {
		t.Fatal(
			"regular slot was not released",
		)
	}

	release()
}

// Control RPCs have their own concurrency limit.
func TestRPCAdmissionControlLimit(
	t *testing.T,
) {
	a := newRPCAdmission()

	for i := 0; i < maxControlRPC; i++ {
		release, ok := a.tryAcquire("StopTest")

		if !ok {
			t.Fatalf(
				"control slot %d rejected",
				i,
			)
		}

		defer release()
	}

	// All control slots are now occupied.
	if release, ok := a.tryAcquire("Stop"); ok {
		release()

		t.Fatal(
			"control limit was not enforced",
		)
	}
}

// Telemetry has independent capacity.
func TestRPCAdmissionTelemetryIsolation(
	t *testing.T,
) {
	a := newRPCAdmission()

	releases := make(
		[]func(),
		0,
		maxTelemetryRPC,
	)

	// Occupy every telemetry slot.
	for i := 0; i < maxTelemetryRPC; i++ {
		release, ok := a.tryAcquire("QueryStats")

		if !ok {
			t.Fatalf(
				"telemetry slot %d rejected",
				i,
			)
		}

		releases = append(releases, release)
	}

	// Additional telemetry must be rejected.
	if release, ok := a.tryAcquire("ListConnections"); ok {
		release()

		t.Fatal(
			"telemetry limit was not enforced",
		)
	}

	// Regular RPCs must still be available.
	releaseTest, ok := a.tryAcquire("Test")

	if !ok {
		t.Fatal(
			"regular RPC blocked by telemetry saturation",
		)
	}

	// Control RPCs must still be available.
	releaseStop, ok := a.tryAcquire("StopTest")

	if !ok {
		t.Fatal(
			"control RPC blocked by telemetry saturation",
		)
	}

	releaseTest()
	releaseStop()

	for _, release := range releases {
		release()
	}
}
