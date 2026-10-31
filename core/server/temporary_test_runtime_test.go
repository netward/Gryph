package main

import (
    "testing"
)

func TestTemporaryRuntimeClose(t *testing.T) {
    runtime, err := newTemporaryTestRuntime(
        lifecycleTestConfig,
        false,
        "",
    )
    if err != nil {
        t.Fatal(err)
    }

    ctx := runtime.box.Context()

    if err := runtime.Close(); err != nil {
        t.Fatalf("Close failed: %v", err)
    }

    if ctx.Err() == nil {
        t.Fatal("runtime context was not cancelled")
    }

    // Closing again should be safe.
    if err := runtime.Close(); err != nil {
        t.Fatalf("second Close failed: %v", err)
    }
}

func TestTemporaryRuntimeInvalidConfig(t *testing.T) {
    runtime, err := newTemporaryTestRuntime(
        "{invalid",
        false,
        "",
    )

    if err == nil {
        if runtime != nil {
            _ = runtime.Close()
        }
        t.Fatal("invalid sing-box config was accepted")
    }

    if runtime != nil {
        t.Fatal("failed runtime was returned")
    }
}

func TestTemporaryRuntimeInvalidXrayConfig(t *testing.T) {
    runtime, err := newTemporaryTestRuntime(
        lifecycleTestConfig,
        true,
        "{invalid",
    )

    if err == nil {
        if runtime != nil {
            _ = runtime.Close()
        }
        t.Fatal("invalid Xray config was accepted")
    }

    if runtime != nil {
        t.Fatal("failed runtime was returned")
    }
}