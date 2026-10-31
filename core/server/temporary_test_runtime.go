package main

import (
	"context"
	"errors"
	"fmt"

	"GryphCore/internal/boxbox"
	"GryphCore/internal/boxmain"
	"GryphCore/internal/xray"

	"github.com/xtls/xray-core/core"
)

type temporaryTestRuntime struct {
	box    *boxbox.Box
	cancel context.CancelFunc
	xray   *core.Instance
}

func newTemporaryTestRuntime(
	boxConfig string,
	needXray bool,
	xrayConfig string,
) (_ *temporaryTestRuntime, err error) {
	runtime := &temporaryTestRuntime{}

	// Roll back everything that was created if any step fails.
	defer func() {
		if err != nil {
			err = errors.Join(err, runtime.Close())
		}
	}()

	if needXray {
		runtime.xray, err = xray.CreateXrayInstance(xrayConfig)
		if err != nil {
			return nil, fmt.Errorf("create temporary Xray: %w", err)
		}

		if err = runtime.xray.Start(); err != nil {
			return nil, fmt.Errorf("start temporary Xray: %w", err)
		}
	}

	runtime.box, runtime.cancel, err = boxmain.Create(
		[]byte(boxConfig),
	)
	if err != nil {
		return nil, fmt.Errorf("create temporary sing-box: %w", err)
	}

	return runtime, nil
}

func (r *temporaryTestRuntime) Close() error {
	if r == nil {
		return nil
	}

	// Cancel before closing network resources.
	if r.cancel != nil {
		r.cancel()
		r.cancel = nil
	}

	var errs []error

	// sing-box may depend on Xray, so close it first.
	if r.box != nil {
		if err := r.box.Close(); err != nil {
			errs = append(errs,
				fmt.Errorf("close temporary sing-box: %w", err))
		}
		r.box = nil
	}

	if r.xray != nil {
		if err := r.xray.Close(); err != nil {
			errs = append(errs,
				fmt.Errorf("close temporary Xray: %w", err))
		}
		r.xray = nil
	}

	return errors.Join(errs...)
}
