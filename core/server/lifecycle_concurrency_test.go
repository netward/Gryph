package main

import (
	"context"
	"fmt"
	"path/filepath"
	"sync"
	"testing"

	"GryphCore/gen"
)

// No TUN, proxy listeners or external servers are required.
const lifecycleTestConfig = `{
	"log": {"level": "error"},
	"outbounds": [
		{"type": "direct", "tag": "direct"}
	],
	"route": {"final": "direct"}
}`

func TestLifecycleConcurrentFailedStarts(t *testing.T) {
	s := &server{}
	ctx := context.Background()

	t.Cleanup(func() {
		_, _ = s.Stop(ctx, &gen.EmptyReq{})
	})

	missingPath := filepath.Join(
		t.TempDir(),
		"missing-extra-core.exe",
	)

	const workers = 8
	const iterations = 24

	start := make(chan struct{})
	failures := make(chan error, workers*iterations)

	var wg sync.WaitGroup

	for worker := 0; worker < workers; worker++ {
		wg.Add(1)

		go func(worker int) {
			defer wg.Done()
			<-start

			for i := 0; i < iterations; i++ {
				switch worker % 4 {
				case 0:
					resp, err := s.Start(
						ctx,
						&gen.LoadConfigReq{
							CoreConfig:       To("{}"),
							NeedExtraProcess: To(true),
							ExtraProcessPath: To(missingPath),
							ExtraNoOut:       To(true),
						},
					)

					if err != nil || resp == nil ||
						resp.GetError() == "" {
						failures <- fmt.Errorf(
							"Start: response=%v err=%v",
							resp, err,
						)
					}

				case 1:
					resp, err := s.Stop(
						ctx,
						&gen.EmptyReq{},
					)

					if err != nil || resp == nil ||
						resp.GetError() != "" {
						failures <- fmt.Errorf(
							"Stop: response=%v err=%v",
							resp, err,
						)
					}

				case 2:
					resp, err := s.QueryStats(
						ctx,
						&gen.EmptyReq{},
					)

					if err != nil || resp == nil {
						failures <- fmt.Errorf(
							"QueryStats: response=%v err=%v",
							resp, err,
						)
					}

				case 3:
					resp, err := s.ListConnections(
						ctx,
						&gen.EmptyReq{},
					)

					if err != nil || resp == nil {
						failures <- fmt.Errorf(
							"ListConnections: response=%v err=%v",
							resp, err,
						)
					}
				}
			}
		}(worker)
	}

	close(start)
	wg.Wait()
	close(failures)

	for err := range failures {
		t.Error(err)
	}

	_, _ = s.Stop(ctx, &gen.EmptyReq{})
	assertRuntimeStopped(t)
}

func TestLifecycleConcurrentRestartWithReaders(t *testing.T) {
	s := &server{}
	ctx := context.Background()

	req := &gen.LoadConfigReq{
		CoreConfig: To(lifecycleTestConfig),
	}

	t.Cleanup(func() {
		_, _ = s.Stop(ctx, &gen.EmptyReq{})
	})

	startCore := func() error {
		resp, err := s.Start(ctx, req)
		if err != nil {
			return err
		}
		if resp == nil {
			return fmt.Errorf("Start returned nil response")
		}
		if resp.GetError() != "" {
			return fmt.Errorf("Start: %s", resp.GetError())
		}
		return nil
	}

	stopCore := func() error {
		resp, err := s.Stop(ctx, &gen.EmptyReq{})
		if err != nil {
			return err
		}
		if resp == nil {
			return fmt.Errorf("Stop returned nil response")
		}
		if resp.GetError() != "" {
			return fmt.Errorf("Stop: %s", resp.GetError())
		}
		return nil
	}

	if err := startCore(); err != nil {
		t.Fatalf("Cannot start test core: %v", err)
	}

	const readers = 6
	const iterations = 24
	const restarts = 5

	start := make(chan struct{})
	failures := make(chan error, readers*iterations+restarts*2)

	var wg sync.WaitGroup

	for reader := 0; reader < readers; reader++ {
		wg.Add(1)

		go func() {
			defer wg.Done()
			<-start

			for i := 0; i < iterations; i++ {
				stats, err := s.QueryStats(
					ctx,
					&gen.EmptyReq{},
				)

				if err != nil || stats == nil {
					failures <- fmt.Errorf(
						"QueryStats: response=%v err=%v",
						stats, err,
					)
				}

				connections, err := s.ListConnections(
					ctx,
					&gen.EmptyReq{},
				)

				// A minimal direct-only configuration
				// has no Clash API server.
				if err != nil &&
					err.Error() != "no clash server found" {
					failures <- fmt.Errorf(
						"ListConnections: %w", err,
					)
				}

				if err == nil && connections == nil {
					failures <- fmt.Errorf(
						"ListConnections returned nil response",
					)
				}
			}
		}()
	}

	wg.Add(1)
	go func() {
		defer wg.Done()
		<-start

		for i := 0; i < restarts; i++ {
			if err := stopCore(); err != nil {
				failures <- err
				return
			}

			if err := startCore(); err != nil {
				failures <- err
				return
			}
		}
	}()

	close(start)
	wg.Wait()
	close(failures)

	for err := range failures {
		t.Error(err)
	}

	if err := stopCore(); err != nil {
		t.Fatal(err)
	}

	assertRuntimeStopped(t)
}