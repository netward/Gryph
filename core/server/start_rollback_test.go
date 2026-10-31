package main

import (
	"context"
	"os"
	"path/filepath"
	"runtime"
	"testing"
	"time"

	"GryphCore/gen"
	"GryphCore/internal/xray"
)

func assertRuntimeStopped(t *testing.T) {
	t.Helper()

	if boxInstance != nil {
		t.Error("sing-box instance was not cleaned up")
	}
	if xrayInstance != nil {
		t.Error("Xray instance was not cleaned up")
	}
	if extraProcess != nil {
		t.Error("extra process was not cleaned up")
	}
	if instanceCancel != nil {
		t.Error("instance cancel function was not cleared")
	}
}

func TestStartRollbackFailedExtraProcess(t *testing.T) {
	missingExecutable := filepath.Join(
		t.TempDir(), "missing-extra-process.exe",
	)

	response, err := (&server{}).Start(
		context.Background(),
		&gen.LoadConfigReq{
			CoreConfig:       To("{}"),
			NeedExtraProcess: To(true),
			ExtraProcessPath: To(missingExecutable),
			ExtraNoOut:       To(true),
		},
	)

	if err != nil {
		t.Fatalf("unexpected RPC error: %v", err)
	}
	if response == nil || response.GetError() == "" {
		t.Fatal("expected extra process startup failure")
	}

	assertRuntimeStopped(t)
}

func TestStartRollbackInvalidXrayConfig(t *testing.T) {
	response, err := (&server{}).Start(
		context.Background(),
		&gen.LoadConfigReq{
			CoreConfig: To("{}"),
			NeedXray:   To(true),
			XrayConfig: To("{invalid"),
		},
	)

	if err != nil {
		t.Fatalf("unexpected RPC error: %v", err)
	}
	if response == nil || response.GetError() == "" {
		t.Fatal("expected Xray configuration error")
	}

	assertRuntimeStopped(t)
}

// The helper is launched as a child of the test process.
// It is used to verify rollback after ExtraProcess starts.
func TestRollbackChildProcess(t *testing.T) {
	if os.Getenv("CORE_ROLLBACK_HELPER") != "1" {
		return
	}

	// Failsafe: do not leave a permanent child process.
	time.Sleep(10 * time.Second)
}

func TestStartRollbackXrayFailureAfterExtraProcess(t *testing.T) {
	if runtime.GOOS != "windows" {
		t.Skip("Windows extra-process integration test")
	}

	executable, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}

	const invalidXrayConfig = "{invalid"

	// Verify the expected error independently.
	_, expectedErr := xray.CreateXrayInstance(
		invalidXrayConfig,
	)
	if expectedErr == nil {
		t.Fatal("invalid Xray config was accepted")
	}

	t.Setenv("CORE_ROLLBACK_HELPER", "1")

	// Clean up even if an assertion fails.
	t.Cleanup(func() {
		if boxInstance != nil ||
			xrayInstance != nil ||
			extraProcess != nil {
			_, _ = (&server{}).Stop(
				context.Background(),
				&gen.EmptyReq{},
			)
		}
	})

	response, rpcErr := (&server{}).Start(
		context.Background(),
		&gen.LoadConfigReq{
			CoreConfig:       To("{}"),
			NeedExtraProcess: To(true),
			ExtraProcessPath: To(executable),
			ExtraProcessArgs: To("-test.run=^TestRollbackChildProcess$"),
			ExtraNoOut:       To(true),
			NeedXray:         To(true),
			XrayConfig:       To(invalidXrayConfig),
		},
	)

	if rpcErr != nil {
		t.Fatalf("unexpected RPC error: %v", rpcErr)
	}
	if response == nil {
		t.Fatal("Start returned nil response")
	}

	if response.GetError() != expectedErr.Error() {
		t.Fatalf(
			"expected Xray parsing error after ExtraProcess start; got %q",
			response.GetError(),
		)
	}

	assertRuntimeStopped(t)
}
