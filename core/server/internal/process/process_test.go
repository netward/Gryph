package process

import (
	"os"
	"path/filepath"
	"testing"
)

func TestFailedExtraProcessStartRemovesOwnedConfig(t *testing.T) {
	temp := t.TempDir()

	configPath := filepath.Join(
		temp,
		"owned-config.json",
	)

	if err := os.WriteFile(
		configPath,
		[]byte(`{"test":true}`),
		0600,
	); err != nil {
		t.Fatal(err)
	}

	executable := filepath.Join(
		temp,
		"missing-gryph-extra-process",
	)

	p := NewProcess(executable, nil, true)
	p.SetCleanupPath(configPath)

	if err := p.Start(); err == nil {
		t.Fatal("expected non-existent executable to fail")
	}

	if _, err := os.Stat(configPath); !os.IsNotExist(err) {
		t.Fatalf(
			"failed process start leaked config: %v",
			err,
		)
	}
}
