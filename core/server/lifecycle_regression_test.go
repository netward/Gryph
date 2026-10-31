package main

import (
	"context"
	"os"
	"strings"
	"testing"

	"GryphCore/gen"
	"GryphCore/internal/boxbox"
	"GryphCore/internal/process"
)

func requireKnownDefects(t *testing.T) {
	t.Helper()

	if os.Getenv("GRYPH_KNOWN_DEFECTS") != "1" {
		t.Skip(
			"enable with GRYPH_KNOWN_DEFECTS=1",
		)
	}
}

func TestKnownDefectRejectedStartMustPreserveRunningInstance(
	t *testing.T,
) {
	requireKnownDefects(t)

	if boxInstance != nil ||
		extraProcess != nil ||
		xrayInstance != nil {
		t.Skip("runtime is active")
	}

	// Имитируем уже запущенное ядро.
	// Реальное сетевое соединение не создаём.
	sentinel := &boxbox.Box{}

	boxInstance = sentinel
	defer func() {
		boxInstance = nil
	}()

	response, err := (&server{}).Start(
		context.Background(),
		&gen.LoadConfigReq{},
	)

	if err != nil {
		t.Fatalf(
			"unexpected transport-level error: %v",
			err,
		)
	}

	if response == nil ||
		!strings.Contains(
			response.GetError(),
			"instance already started",
		) {
		t.Fatalf(
			"expected already-started error, got %+v",
			response,
		)
	}

	if boxInstance != sentinel {
		t.Fatal(
			"rejected Start lost running instance pointer",
		)
	}
}

func TestKnownDefectStopMustHandleExtraProcessWithoutBox(
	t *testing.T,
) {
	requireKnownDefects(t)

	if boxInstance != nil ||
		extraProcess != nil ||
		xrayInstance != nil {
		t.Skip("runtime is active")
	}

	extraProcess = process.NewProcess(
		"not-started",
		nil,
		true,
	)

	defer func() {
		extraProcess = nil
	}()

	response, err := (&server{}).Stop(
		context.Background(),
		&gen.EmptyReq{},
	)

	if err != nil ||
		response == nil ||
		response.GetError() != "" {
		t.Fatalf(
			"unexpected Stop response=%+v err=%v",
			response,
			err,
		)
	}

	if extraProcess != nil {
		t.Fatal(
			"Stop returned without cleaning ExtraProcess",
		)
	}
}